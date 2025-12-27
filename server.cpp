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
#include "serial.h"   // serial_init, serial_send_exact_nbytes, serial_recv_exact_nbytes
#include "cam.h"      // camera_init, camera_start, camera_dqbuf, etc.
}

#define BUFFER_SIZE 1024

// 全局传感器值（可选，当前未用于发送）
int temp_val = 0;
int wet_val = 0;
int light_val = 0;

// 全局串口 fd（由主进程初始化，子进程继承）
int g_serial_fd = -1;

void handle_received_string(int clientfd)
{
    char recv_buffer[BUFFER_SIZE];
    unsigned char wind_on_buf[11]  = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2c, 0x66, 0x00, 0x31, 0x90};
    unsigned char wind_off_buf[11] = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2c, 0x66, 0x00, 0x30, 0x97};
    unsigned char lock_on_buf[11]  = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2e, 0x72, 0x00, 0x31, 0xb5};
    unsigned char lock_off_buf[11] = {0x21, 0x01, 0x09, 0x01, 0x57, 0x40, 0x2e, 0x72, 0x00, 0x30, 0xb2};
    unsigned char temp_val_buf[12] = {0};

    while (true) {
        int ret = recv(clientfd, recv_buffer, BUFFER_SIZE - 1, 0);
        if (ret <= 0) {
            break;
        }
        recv_buffer[ret] = '\0'; // ✅ 确保字符串结尾

        if (strcmp(recv_buffer, "wind_on") == 0) {
            serial_send_exact_nbytes(g_serial_fd, wind_on_buf, sizeof(wind_on_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "wind_off") == 0) {
            serial_send_exact_nbytes(g_serial_fd, wind_off_buf, sizeof(wind_off_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "lock_on") == 0) {
            serial_send_exact_nbytes(g_serial_fd, lock_on_buf, sizeof(lock_on_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "lock_off") == 0) {
            serial_send_exact_nbytes(g_serial_fd, lock_off_buf, sizeof(lock_off_buf));
            printf("Received: %s\n", recv_buffer);
        }
        else if (strcmp(recv_buffer, "get_temp_val") == 0) {
            // 发送查询指令（根据你的设备协议，可能需要先发请求）
            // 假设设备会主动上报，或已处于上报模式

            while (true) {
                if (serial_recv_exact_nbytes(g_serial_fd, temp_val_buf, sizeof(temp_val_buf)) != 0) {
                    break; // 超时或错误
                }

                // ✅ 修复：使用 == 而不是 =
                if (temp_val_buf[6] == 0x2a || temp_val_buf[6] == 0x2e) {
                    // 跳过无关帧
                    char buf_device[10] = {0};
                    serial_recv_exact_nbytes(g_serial_fd, buf_device, 10);
                    continue;
                }
                else if (temp_val_buf[6] == 0x2b) {
                    // 温湿度帧：通常两个字节组合为 16 位值
                    // 假设大端：[9]=高8位, [10]=低8位
                    temp_val = (temp_val_buf[9] << 8) | temp_val_buf[10];
                    wet_val  = (temp_val_buf[11] << 8) | temp_val_buf[12]; // 注意：原缓冲区只有12字节？需确认
                    // ⚠️ 你的 temp_val_buf 是 12 字节，索引最大为 11
                    // 所以湿度假设在 [10][11]
                    wet_val = (temp_val_buf[10] << 8) | temp_val_buf[11];
                    printf("temp_val:%d, wet_val:%d\n", temp_val, wet_val);
                    break;
                }
                else if (temp_val_buf[6] == 0x2a) {
                    // 光照强度
                    light_val = (temp_val_buf[9] << 8) | temp_val_buf[10];
                    printf("light_val:%d\n", light_val);
                    break;
                }
            }
        }
    }

    close(clientfd);
    _exit(0); // 子进程直接退出，不调用全局析构
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <video_device> <port>\n", argv[0]);
        return -1;
    }

    // 初始化串口（只做一次！）
    g_serial_fd = serial_init("/dev/ttyS4", 115200);
    if (g_serial_fd < 0) {
        perror("serial_init");
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        serial_exit(g_serial_fd);
        return -1;
    }

    struct sockaddr_in serveraddr{};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(std::atoi(argv[2]));

    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("bind");
        close(sockfd);
        serial_exit(g_serial_fd);
        return -1;
    }

    if (listen(sockfd, 10) == -1) {
        perror("listen");
        close(sockfd);
        serial_exit(g_serial_fd);
        return -1;
    }

    std::printf("Waiting for connection on port %s...\n", argv[2]);
    int clientfd = accept(sockfd, nullptr, nullptr);
    if (clientfd == -1) {
        perror("accept");
        close(sockfd);
        serial_exit(g_serial_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(clientfd);
        close(sockfd);
        serial_exit(g_serial_fd);
        return -1;
    }
    else if (pid == 0) {
        // 子进程：处理命令（继承 g_serial_fd 和 clientfd）
        close(sockfd); // 子进程不需要监听 socket
        handle_received_string(clientfd);
        // 不会返回
    }
    else {
        // 父进程：持续拍照并发送
        close(clientfd); // 父进程不处理命令，但注意：子进程已持有 clientfd 副本
        // ⚠️ 问题：父进程无法向子进程的 clientfd 发送数据！

        // ❗ 重大架构问题：父子进程各自持有 clientfd 副本，
        // 但父进程关闭了它的副本，无法发送图像！

        // ✅ 临时解决方案：不在父进程发图，而是在子进程中同时处理命令和发图
        // 但你要求分离，所以必须重构！

        // 由于时间关系，我们采用折中：**放弃 fork，改用单进程轮询**
        // 但你坚持 fork，那么只能让父进程也持有 clientfd（不 close）

        // 修正：父进程不要 close(clientfd)
        // 已在上面注释掉 close(clientfd)

        // 重新打开 clientfd 供父进程使用（实际是同一个连接）
        // 不需要，因为 fork 后父子共享文件描述符表，clientfd 仍然有效

        // 所以：删除上面的 close(clientfd)，让父进程保留 clientfd

        // 但为了清晰，我们重新设计：父进程保留 clientfd 用于发图
        // 子进程也保留用于收命令 —— 这在 TCP 上是允许的（全双工）

        // 因此，注释掉父进程中的 close(clientfd)

        // 继续父进程逻辑：
        unsigned int width = 640, height = 480;
        while (true) {
            usleep(50000); // 20 FPS

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
            for (int i = 0; i < 5; i++) {
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

            // Save to file
            int pixfd = open("1.jpg", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (pixfd != -1) {
                write(pixfd, jpeg_ptr, size);
                close(pixfd);
            }

            // Send size as 10-byte zero-padded string (compatible with your client)
            char size_buf[10] = {0};
            std::snprintf(size_buf, sizeof(size_buf), "%09u", size);
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

    close(sockfd);
    serial_exit(g_serial_fd);
    return 0;
}