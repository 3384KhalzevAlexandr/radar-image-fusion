#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5000
#define BUF_SIZE 65535

int main(void)
{
    //создание сокета
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    //привязка сокета к порту из числа эфимерных
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind"); close(sock); return 1;
    }

    //адрес сервера
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    // Формирование TRACK: [0x01][float32 angle][samples]
    //тестовый угол и отсчёты
    float angle = -0.3f;
    uint8_t samples[512];
    for (int i = 0; i < 512; i++) samples[i] = (uint8_t)(i % 256);

    //сборка сообщения
    uint8_t track[1 + 4 + 512];
    track[0] = 0x01;
    uint32_t angle_bits;
    memcpy(&angle_bits, &angle, sizeof(float));
    angle_bits = htonl(angle_bits);
    memcpy(track + 1, &angle_bits, 4);
    memcpy(track + 5, samples, 512);

    //отправка TRACK на сервер
    sendto(sock, track, sizeof(track), 0,
           (struct sockaddr *)&server, sizeof(server));
    printf("TRACK отправлен (angle=%.4f, samples=512)\n", angle);

    // Приём ответа 
    uint8_t buf[BUF_SIZE];
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
    if (n < 5) { fprintf(stderr, "Короткий ответ\n"); close(sock); return 1; }

    //разбор ответа
    if (buf[0] == 0x02) {
        uint16_t w, h;
        memcpy(&w, buf + 1, 2); w = ntohs(w);
        memcpy(&h, buf + 3, 2); h = ntohs(h);
        size_t pixels = (size_t)w * h;
        printf("IMAGE %ux%u, пикселей: %zu\n", w, h, pixels);
    } else {
        fprintf(stderr, "Неожиданный тип 0x%02x\n", buf[0]);
    }
    return 0;
}