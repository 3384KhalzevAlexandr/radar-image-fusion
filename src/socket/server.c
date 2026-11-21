#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 5000
#define BUF_SIZE 65535

//функция формирования изображения (для примера)
static void build_image(float angle, const uint8_t *samples, size_t n,
                        uint16_t *width, uint16_t *height, uint8_t **pixels)
{
    (void)angle;
    *width = (uint16_t)n;
    *height = 1;
    *pixels = malloc(n);
    memcpy(*pixels, samples, n);
}

int main(void)
{
    //создание сокета
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    //привязка сокета к порту
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return 1;
    }
    printf("UDP сервер слушает 0.0.0.0:%d\n", PORT);

    //буфер для входящих датаграмм
    uint8_t buf[BUF_SIZE];
    //бесконечный цикл приёма сообщений
    for (;;) {
        //адрес отправителя 
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&client, &client_len);

        //первый байт - тип сообщения
        uint8_t type = buf[0];
        if (type != 0x01) {
            fprintf(stderr, "Неизвестный тип 0x%02x\n", type);
            continue;
        }

        // Угол 4 байта big-endian float32 
        uint32_t angle_bits;
        memcpy(&angle_bits, buf + 1, 4);
        angle_bits = ntohl(angle_bits);
        float angle;
        memcpy(&angle, &angle_bits, sizeof(float));

        //отсчёты сигнала
        const uint8_t *samples = buf + 5;
        size_t sample_count = (size_t)n - 5;

        //печать адреса отправителя и параметров дорожки
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
        printf("TRACK от %s:%d: angle=%.4f рад, samples=%zu\n",
               ip, ntohs(client.sin_port), angle, sample_count);

        // Обработка 
        uint16_t width, height;
        uint8_t *pixels;
        build_image(angle, samples, sample_count, &width, &height, &pixels);

        // Ответ: [0x02][uint16 width][uint16 height][pixels]
        size_t resp_len = 1 + 2 + 2 + (size_t)width * height;
        uint8_t *resp = malloc(resp_len);
        resp[0] = 0x02;
        uint16_t w = htons(width), h = htons(height);
        memcpy(resp + 1, &w, 2);
        memcpy(resp + 3, &h, 2);
        memcpy(resp + 5, pixels, (size_t)width * height);

        //отправка ответа на адрес отправителя
        sendto(sock, resp, resp_len, 0,
               (struct sockaddr *)&client, client_len);

        free(pixels);
        free(resp);
    }
    return 0;
}