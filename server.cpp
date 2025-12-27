// server.cpp
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <csignal>
#include <fcntl.h>
#include <string>

extern "C" {
#include "serial.h"   // 假设 serial.h 是 C 接口
#include "cam.h"
}

#define BUFFER_SIZE 1024

// 全局传感器值（属于业务逻辑）
int temp_val = 0;
int wet_val = 0;
int light_val = 0;

void handle_received_string(int clientfd)
{
    int serial_fd = serial_init("/dev/ttyS4", 115200);
    char recv_buffer[BUFFER_SIZE];
    unsigned char wind_on_buf[11]  = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2c, 0x66, 0x00, 0x31, 0x90};
    unsigned char wind_off_buf[11] = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2c, 0x66, 0x00, 0x30, 0x97};
    unsigned char lock_on_buf[11]  = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2e, 0x72, 0x00, 0x31, 0xb5};
    unsigned char lock_off_buf[11] = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2e, 0x72, 0x00, 0x30, 0xb2};
    unsigned char temp_val_buf[12] = {0};

    sleep(1);

    while (true) {
        int ret = recv(clientfd, recv_buffer, BUFFER_SIZE - 1, 0);
        if (ret <= 0) {
            perror("recv");
            break;
        }
        recv_buffer[ret] = '\0'; // 确保字符串结尾

        if (strcmp(recv_buffer, "wind_on") == 0) {
            serial_send_exact_nbytes(serial_fd, wind_on_buf, sizeof(wind_on_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "wind_off") == 0) {
            serial_send_exact_nbytes(serial_fd, wind_off_buf, sizeof(wind_off_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "lock_on") == 0) {
            serial_send_exact_nbytes(serial_fd, lock_on_buf, sizeof(lock_on_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "lock_off") == 0) {
            serial_send_exact_nbytes(serial_fd, lock_off_buf, sizeof(lock_off_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "get_temp_val") == 0) {
            while (true) {
                serial_recv_exact_nbytes(serial_fd, temp_val_buf, sizeof(temp_val_buf));
                if (temp_val_buf[6] == 0x2a || temp_val_buf[6] == 0x2e) {
                    char buf_device[10] = {0};
                    serial_recv_exact_nbytes(serial_fd, buf_device, 10);
                    continue;
                }
                else if (temp_val_buf[6] == 0x2b) {  // 注意：原代码用了赋值 =，应为比较 ==
                    temp_val = (int)temp_val_buf[9] + ((int)temp_val_buf[10] >> 4); // 修正位运算逻辑？
                    wet_val = (int)temp_val_buf[10] + ((int)temp_val_buf[11] >> 4);
                    printf("temp_val:%d, wet_val:%d\n", temp_val, wet_val);
                    break;
                }
                else if (temp_val_buf[6] == 0x2a) {
                    light_val = (temp_val_buf[9] * 256 + temp_val_buf[10]) / 2;
                    printf("light_val:%d\n", light_val);
                    break;
                }
            }
        }
    }

    close(clientfd);
    serial_exit(serial_fd); // 如果 serial.h 提供此函数
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <video_device> <port>\n", argv[0]);
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in serveraddr{};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(std::atoi(argv[2]));

    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 10) == -1) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    std::printf("Waiting for connection...\n");
    int clientfd = accept(sockfd, nullptr, nullptr);
    if (clientfd == -1) {
        perror("accept");
        close(sockfd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(clientfd);
        close(sockfd);
        return -1;
    }
    else if (pid == 0) {
        // 子进程：处理命令
        handle_received_string(clientfd);
        exit(0);
    }
    else {
        // 父进程：持续拍照并发送
        unsigned int width = 640, height = 480;
        while (true) {
            usleep(50000); // 50ms

            unsigned int size = 0, index = 0, ismjpeg = 0;
            int fd = camera_init(argv[1], &width, &height, &size, &ismjpeg);
            if (fd == -1) {
                std::fprintf(stderr, "Camera init failed\n");
                break;
            }

            if (camera_start(fd) == -1) {
                camera_exit(fd);
                break;
            }

            // Drain initial frames
            void *jpeg_ptr = nullptr;
            for (int i = 0; i < 8; i++) {
                if (camera_dqbuf(fd, &jpeg_ptr, &size, &index) == -1 ||
                    camera_eqbuf(fd, index) == -1) {
                    camera_stop(fd);
                    camera_exit(fd);
                    goto parent_exit;
                }
            }

            // Capture one frame
            if (camera_dqbuf(fd, &jpeg_ptr, &size, &index) == -1) {
                camera_stop(fd);
                camera_exit(fd);
                break;
            }

            // Save to file (optional)
            int pixfd = open("1.jpg", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (pixfd != -1) {
                write(pixfd, jpeg_ptr, size);
                close(pixfd);
            }

            // Send size (10 bytes, zero-padded)
            char size_buf[10] = {0};
            std::snprintf(size_buf, sizeof(size_buf), "%09d", size); // 9 digits + \0
            if (write(clientfd, size_buf, 10) != 10) {
                perror("send size");
                break;
            }

            // Send image data
            if (write(clientfd, jpeg_ptr, size) != (ssize_t)size) {
                perror("send image");
                break;
            }

            camera_eqbuf(fd, index);
            camera_stop(fd);
            camera_exit(fd);
        }
parent_exit:
        kill(pid, SIGTERM);
        wait(nullptr);
    }

    close(clientfd);
    close(sockfd);
    return 0;
}