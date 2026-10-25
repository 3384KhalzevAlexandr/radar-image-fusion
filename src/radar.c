#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PI 3.14159265358979323846
#define POINT_RADIUS 2          
#define MAX_IMG_DIM 1000        

typedef struct {
    double angle;   
    double range;   
    double value;
    int valid;
} Measurement;

typedef struct {
    unsigned char r, g, b;
} Pixel;

typedef struct {
    int w, h;
    Pixel *data;
} Image;

static Measurement *data = NULL;
static size_t count = 0;

static double min_range, max_range;
static double min_value, max_value;

static int load_csv(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) { perror("Cannot open CSV"); return 0; }

    char line[512];
    double angle, range, value;
    int track, sample;

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%d,%lf,%d,%lf,%lf",
                   &track, &angle, &sample, &range, &value) != 5)
            continue;

        Measurement *tmp = realloc(data, (count + 1) * sizeof(Measurement));
        if (!tmp) { fprintf(stderr, "Out of memory\n"); fclose(file); return 0; }
        data = tmp;

        data[count].angle = angle;
        data[count].range = range;
        data[count].value = value;
        data[count].valid   = 1;

        if (count == 0) {
            min_range = max_range = range;
            min_value = max_value = value;
        } else {
            if (range < min_range) min_range = range;
            if (range > max_range) max_range = range;
            if (value < min_value) min_value = value;
            if (value > max_value) max_value = value;
        }
        count++;
    }
    fclose(file);
    return count > 0;
}

static void polar_to_xy(double angle_deg, double range,
                        double *out_x, double *out_y)
{
    double a = angle_deg * PI / 180.0;
    *out_x = range * sin(a);
    *out_y = range * cos(a);
}

static Pixel value_to_color(double value)
{
    double t = 0.5;
    if (max_value > min_value)
        t = (value - min_value) / (max_value - min_value);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    Pixel c;
    if (t < 0.5) {
        double k = t * 2.0;
        c.r = (unsigned char)(255 * k);
        c.g = (unsigned char)(255 * k);
        c.b = 255;
    } else {
        double k = (t - 0.5) * 2.0;
        c.r = 255;
        c.g = (unsigned char)(255 * (1.0 - k));
        c.b = (unsigned char)(255 * (1.0 - k));
    }
    return c;
}

static void image_init(Image *im, int w, int h)
{
    im->w = w; im->h = h;
    im->data = malloc((size_t)w * h * sizeof(Pixel));
    Pixel bg = {255, 255, 255};
    for (size_t i = 0; i < (size_t)w * h; i++)
        im->data[i] = bg;
}

static void set_pixel(Image *im, int x, int y, Pixel c)
{
    if (x < 0 || x >= im->w || y < 0 || y >= im->h) return;
    im->data[(size_t)y * im->w + x] = c;
}

static void draw_point(Image *im, int x, int y, Pixel c)
{
    for (int dy = -POINT_RADIUS; dy <= POINT_RADIUS; dy++)
        for (int dx = -POINT_RADIUS; dx <= POINT_RADIUS; dx++)
            if (dx*dx + dy*dy <= POINT_RADIUS*POINT_RADIUS)
                set_pixel(im, x + dx, y + dy, c);
}

static void build_cartesian(Image *im)
{
    double xmin =  1e30, xmax = -1e30;
    double ymin =  1e30, ymax = -1e30;

    for (size_t i = 0; i < count; i++) {
        if (!data[i].valid) continue;
        double x, y;
        polar_to_xy(data[i].angle, data[i].range, &x, &y);
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
    }

    double span_x = xmax - xmin;
    double span_y = ymax - ymin;
    if (span_x <= 0) span_x = 1;
    if (span_y <= 0) span_y = 1;

    printf("Ground extent: X = %.2f .. %.2f  (span %.2f)\n", xmin, xmax, span_x);
    printf("               Y = %.2f .. %.2f  (span %.2f)\n", ymin, ymax, span_y);

    double aspect = span_x / span_y;
    int W, H;
    if (aspect >= 1.0) {
        W = MAX_IMG_DIM;
        H = (int)(MAX_IMG_DIM / aspect);
        if (H < 1) H = 1;
    } else {
        H = MAX_IMG_DIM;
        W = (int)(MAX_IMG_DIM * aspect);
        if (W < 1) W = 1;
    }
    fprintf(stderr, "Image: %dx%d\n", W, H);

    image_init(im, W, H);

    double scale_x = (W - 1) / span_x;
    double scale_y = (H - 1) / span_y;

    for (size_t i = 0; i < count; i++) {
        if (!data[i].valid) continue;
        double x, y;
        polar_to_xy(data[i].angle, data[i].range, &x, &y);

        int px = (int)round((x - xmin) * scale_x);
        int py = (int)round((ymax - y) * scale_y);

        Pixel c = value_to_color(data[i].value);
        draw_point(im, px, py, c);
    }
}

static int save_ppm(const char *filename, const Image *im)
{
    FILE *f = fopen(filename, "w");
    if (!f) { perror("Cannot save image"); return 0; }

    fprintf(f, "P3\n%d %d\n255\n", im->w, im->h);
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            Pixel p = im->data[(size_t)y * im->w + x];
            fprintf(f, "%d %d %d ", p.r, p.g, p.b);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    const char *in  = (argc > 1) ? argv[1] : "../data/radar.csv";
    const char *out = (argc > 2) ? argv[2] : "../data/radar_cartesian.ppm";

    if (!load_csv(in)) {
        fprintf(stderr, "No valid measurements in %s\n", in);
        free(data);
        return 1;
    }

    printf("Loaded %zu measurements from %s\n", count, in);
    printf("Range: %.2f .. %.2f\n", min_range, max_range);
    printf("Value: %.2f .. %.2f\n", min_value, max_value);

    Image im;
    build_cartesian(&im);

    if (!save_ppm(out, &im)) {
        free(im.data); free(data);
        return 1;
    }
    printf("Saved %s\n", out);

    free(im.data);
    free(data);
    return 0;
}