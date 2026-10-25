#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <tiffio.h>

typedef struct { int w, h; float *data; } Image;

typedef struct { double re, im; } cplx;

Image load_tiff(const char *path) {
    Image img = {0, 0, NULL};
    TIFF *tif = TIFFOpen(path, "r");
    if (!tif) { fprintf(stderr, "Cannot open %s\n", path); return img; }

    uint32_t w = 0, h = 0;
    uint16_t bps = 8, spp = 1, fmt = SAMPLEFORMAT_UINT, planar = PLANARCONFIG_CONTIG;

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) || w == 0 || h == 0) {
        fprintf(stderr, "Bad dims %s\n", path); TIFFClose(tif); return img;
    }
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &fmt);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
    if (planar == PLANARCONFIG_SEPARATE) spp = 1;

    fprintf(stderr, "load %s: %ux%u bps=%u spp=%u fmt=%u tiled=%d\n",
            path, w, h, bps, spp, fmt, TIFFIsTiled(tif));

    img.w = (int)w; img.h = (int)h;
    img.data = calloc((size_t)w * h, sizeof(float));
    if (!img.data) { TIFFClose(tif); return (Image){0,0,NULL}; }

    size_t bps_bytes = bps / 8; if (bps_bytes == 0) bps_bytes = 1;

    #define COPY_PIXEL(dst, src_ptr) do { \
        double _v = 0; \
        if (bps == 8  && fmt == SAMPLEFORMAT_UINT)        _v = *(uint8_t *)(src_ptr); \
        else if (bps == 16 && fmt == SAMPLEFORMAT_UINT)   _v = *(uint16_t*)(src_ptr); \
        else if (bps == 32 && fmt == SAMPLEFORMAT_UINT)   _v = *(uint32_t*)(src_ptr); \
        else if (bps == 32 && fmt == SAMPLEFORMAT_IEEEFP) _v = *(float   *)(src_ptr); \
        else if (bps == 64 && fmt == SAMPLEFORMAT_IEEEFP) _v = *(double  *)(src_ptr); \
        dst = (float)_v; \
    } while(0)

    if (TIFFIsTiled(tif)) {
        uint32_t tw = 0, th = 0;
        if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw) ||
            !TIFFGetField(tif, TIFFTAG_TILELENGTH, &th) || !tw || !th) {
            fprintf(stderr, "Bad tile geom\n"); TIFFClose(tif); free(img.data);
            return (Image){0,0,NULL};
        }
        tmsize_t tsz = TIFFTileSize(tif);
        uint8_t *tile = (uint8_t*)_TIFFmalloc(tsz);
        for (uint32_t ty = 0; ty < h; ty += th)
            for (uint32_t tx = 0; tx < w; tx += tw) {
                if (TIFFReadTile(tif, tile, tx, ty, 0, 0) < 0) continue;
                for (uint32_t row = 0; row < th && (ty+row) < h; row++)
                    for (uint32_t col = 0; col < tw && (tx+col) < w; col++) {
                        uint8_t *src = tile + ((size_t)row*tw + col)*spp*bps_bytes;
                        float v; COPY_PIXEL(v, src);
                        img.data[(size_t)(ty+row)*w + (tx+col)] = v;
                    }
            }
        _TIFFfree(tile);
    } else {
        tmsize_t ssz = TIFFScanlineSize(tif);
        uint8_t *line = (uint8_t*)_TIFFmalloc(ssz);
        for (uint32_t row = 0; row < h; row++) {
            if (TIFFReadScanline(tif, line, row, 0) < 0) continue;
            for (uint32_t col = 0; col < w; col++) {
                uint8_t *src = line + (size_t)col*spp*bps_bytes;
                float v; COPY_PIXEL(v, src);
                img.data[(size_t)row*w + col] = v;
            }
        }
        _TIFFfree(line);
    }
    #undef COPY_PIXEL
    TIFFClose(tif);
    return img;
}

static void fft_radix2(cplx *a, int n, int inverse) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cplx t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2 * M_PI / len * (inverse ? 1 : -1);
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len/2; k++) {
                cplx u = a[i + k];
                cplx v = { a[i + k + len/2].re * cr - a[i + k + len/2].im * ci,
                           a[i + k + len/2].re * ci + a[i + k + len/2].im * cr };
                a[i + k].re       = u.re + v.re;
                a[i + k].im       = u.im + v.im;
                a[i + k + len/2].re = u.re - v.re;
                a[i + k + len/2].im = u.im - v.im;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < n; i++) { a[i].re /= n; a[i].im /= n; }
}

static void fft2d(cplx *buf, int W, int H, int inverse) {
    for (int y = 0; y < H; y++) fft_radix2(buf + (size_t)y * W, W, inverse);
    cplx *col = malloc(H * sizeof(cplx));
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) col[y] = buf[(size_t)y * W + x];
        fft_radix2(col, H, inverse);
        for (int y = 0; y < H; y++) buf[(size_t)y * W + x] = col[y];
    }
    free(col);
}

static int next_pow2(int n) {
    int p = 1; while (p < n) p <<= 1; return p;
}

void phase_correlation(const Image *A, const Image *B,
                       double *out_dx, double *out_dy) {
    *out_dx = *out_dy = 0.0;
    if (A->w != B->w || A->h != B->h) {
        fprintf(stderr, "phase_correlation: sizes differ (%dx%d vs %dx%d)\n",
                A->w, A->h, B->w, B->h);
        return;
    }
    int W0 = A->w, H0 = A->h;
    int W = next_pow2(W0), H = next_pow2(H0);
    fprintf(stderr, "phase_correlation: %dx%d -> FFT %dx%d\n", W0, H0, W, H);

    size_t n = (size_t)W * H;
    cplx *fa = calloc(n, sizeof(cplx));
    cplx *fb = calloc(n, sizeof(cplx));
    if (!fa || !fb) { free(fa); free(fb); return; }

    double ma = 0, mb = 0;
    for (size_t i = 0; i < (size_t)W0*H0; i++) { ma += A->data[i]; mb += B->data[i]; }
    ma /= (double)W0 * H0;
    mb /= (double)W0 * H0;

    for (int y = 0; y < H0; y++) {
        double wy = 0.5 - 0.5 * cos(2 * M_PI * y / (H0 > 1 ? H0 - 1 : 1));
        for (int x = 0; x < W0; x++) {
            double wx = 0.5 - 0.5 * cos(2 * M_PI * x / (W0 > 1 ? W0 - 1 : 1));
            double win = wx * wy;
            fa[(size_t)y*W + x].re = (A->data[(size_t)y*W0 + x] - ma) * win;
            fb[(size_t)y*W + x].re = (B->data[(size_t)y*W0 + x] - mb) * win;
        }
    }

    fft2d(fa, W, H, 0);
    fft2d(fb, W, H, 0);

    for (size_t i = 0; i < n; i++) {
        double ar = fa[i].re, ai = fa[i].im;
        double br = fb[i].re, bi = -fb[i].im;   
        double cr = ar*br - ai*bi;
        double ci = ar*bi + ai*br;
        double mag = sqrt(cr*cr + ci*ci);
        if (mag > 1e-12) { cr /= mag; ci /= mag; } else { cr = ci = 0; }
        fa[i].re = cr; fa[i].im = ci;
    }
    free(fb);

    fft2d(fa, W, H, 1);

    double best = -1; int bx = 0, by = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double m = fa[(size_t)y*W + x].re * fa[(size_t)y*W + x].re
                     + fa[(size_t)y*W + x].im * fa[(size_t)y*W + x].im;
            if (m > best) { best = m; bx = x; by = y; }
        }

    int sx = (bx <= W/2) ? bx : bx - W;
    int sy = (by <= H/2) ? by : by - H;

    double v0 = sqrt(best);
    int xm = (bx - 1 + W) % W, xp = (bx + 1) % W;
    int ym = (by - 1 + H) % H, yp = (by + 1) % H;
    double vxm = hypot(fa[(size_t)by*W + xm].re, fa[(size_t)by*W + xm].im);
    double vxp = hypot(fa[(size_t)by*W + xp].re, fa[(size_t)by*W + xp].im);
    double vym = hypot(fa[(size_t)ym*W + bx].re, fa[(size_t)ym*W + bx].im);
    double vyp = hypot(fa[(size_t)yp*W + bx].re, fa[(size_t)yp*W + bx].im);

    double sub_x = 0, sub_y = 0;
    double dx_den = 2 * (2*v0 - vxm - vxp);
    double dy_den = 2 * (2*v0 - vym - vyp);
    if (fabs(dx_den) > 1e-9) sub_x = (vxp - vxm) / dx_den;
    if (fabs(dy_den) > 1e-9) sub_y = (vyp - vym) / dy_den;

    *out_dx = sx + sub_x;
    *out_dy = sy + sub_y;

    fprintf(stderr, "phase: peak at (%d,%d), |R|=%.4g, sub=(%.3f, %.3f)\n",
            bx, by, v0, sub_x, sub_y);

    free(fa);
}

void save_blend(const char *path, const Image *A, const Image *B,
                double dx, double dy) {
    TIFF *out = TIFFOpen(path, "w");
    if (!out) { fprintf(stderr, "Cannot create %s\n", path); return; }
    TIFFSetField(out, TIFFTAG_IMAGEWIDTH, A->w);
    TIFFSetField(out, TIFFTAG_IMAGELENGTH, A->h);
    TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));

    float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
    size_t n = (size_t)A->w * A->h;
    for (size_t i = 0; i < n; i++) {
        if (A->data[i] < amin) amin = A->data[i];
        if (A->data[i] > amax) amax = A->data[i];
        if (B->data[i] < bmin) bmin = B->data[i];
        if (B->data[i] > bmax) bmax = B->data[i];
    }
    float arange = (amax > amin) ? (amax - amin) : 1.0f;
    float brange = (bmax > bmin) ? (bmax - bmin) : 1.0f;

    uint8_t *row = malloc((size_t)A->w * 3);
    int W = A->w, H = A->h;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float va = (A->data[(size_t)y*W + x] - amin) / arange;
            double bx = x - dx, by = y - dy;
            float vb = 0;
            if (bx >= 0 && by >= 0 && bx < W-1 && by < H-1) {
                int x0 = (int)bx, y0 = (int)by;
                float fx = (float)(bx - x0), fy = (float)(by - y0);
                float b00 = B->data[(size_t)y0*W + x0];
                float b10 = B->data[(size_t)y0*W + x0 + 1];
                float b01 = B->data[(size_t)(y0+1)*W + x0];
                float b11 = B->data[(size_t)(y0+1)*W + x0 + 1];
                vb = b00*(1-fx)*(1-fy) + b10*fx*(1-fy)
                   + b01*(1-fx)*fy     + b11*fx*fy;
                vb = (vb - bmin) / brange;
            }
            if (va < 0) va = 0;
            if (va > 1) va = 1;
            if (vb < 0) vb = 0;
            if (vb > 1) vb = 1;

            row[x*3 + 0] = (uint8_t)(va * 255.0f);
            row[x*3 + 1] = (uint8_t)(vb * 255.0f);
            row[x*3 + 2] = 0;
        }
        if (TIFFWriteScanline(out, row, y, 0) < 0)
            fprintf(stderr, "TIFFWriteScanline failed at row %d\n", y);
    }
    free(row);
    TIFFClose(out);
    fprintf(stderr, "Saved %s\n", path);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s a.tif b.tif out.tif\n", argv[0]);
        return 1;
    }
    Image A = load_tiff(argv[1]);
    Image B = load_tiff(argv[2]);
    if (!A.data || !B.data || A.w <= 0 || A.h <= 0 || B.w <= 0 || B.h <= 0) {
        fprintf(stderr, "Load failed\n");
        free(A.data); free(B.data);
        return 1;
    }
    if (A.w != B.w || A.h != B.h) {
        fprintf(stderr, "Sizes differ: A=%dx%d B=%dx%d\n", A.w, A.h, B.w, B.h);
        free(A.data); free(B.data);
        return 1;
    }

    double dx, dy;
    phase_correlation(&A, &B, &dx, &dy);

    printf("dx = %.3f px, dy = %.3f px\n", dx, dy);
    printf("B(x,y) ~= A(x + %.3f, y + %.3f)\n", dx, dy);
    printf("To get B from A, shift A by (-%.3f, -%.3f)\n", dx, dy);

    save_blend(argv[3], &A, &B, dx, dy);

    free(A.data); free(B.data);
    return 0;
}