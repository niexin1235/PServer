#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <sys/select.h>

// ========================
// 设备状态全局开关
// ========================
volatile bool g_fan_on = false;
volatile bool g_th_on = false;
volatile bool g_camera_on = false;
volatile bool g_ma_on = false;

// ========================
// 【临时占位实现】获取最新设备数据的接口
// 请在后续替换为你自己的硬件读取逻辑
// ========================
bool get_latest_camera_frame(std::vector<uint8_t>& /*frame*/) {
    // TODO: 从摄像头读取一帧数据到 frame
    return false; // 暂时返回 false 表示无数据
}

bool get_latest_th_data(std::string& data) {
    // TODO: 读取温湿度，格式化到 data
    data = "TH_DATA: T=25.0C H=60%";
    return true;
}

bool get_latest_fan_data(std::string& data) {
    // TODO: 读取风扇状态
    data = "FAN_SPEED: 1200 RPM";
    return true;
}

bool get_latest_ma_data(std::string& data) {
    // TODO: 读取机械臂状态
    data = "ARM_POS: X=100 Y=200 Z=50";
    return true;
}

// ========================
// 设备任务函数
// ========================
void* fan_task(void* param) {
    (void)param; // 消除警告
    while (g_fan_on) {
        usleep(100000);
    }
    return nullptr;
}

void* th_task(void* param) {
    (void)param;
    while (g_th_on) {
        usleep(500000);
    }
    return nullptr;
}

void* camera_task(void* param) {
    (void)param;
    while (g_camera_on) {
        usleep(33000);
    }
    return nullptr;
}

void* ma_task(void* param) {
    (void)param;
    while (g_ma_on) {
        usleep(200000);
    }
    return nullptr;
}

// ========================
// 辅助函数
// ========================
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

void send_error(int client_sock, const std::string& err_code) {
    std::string msg = "ERROR: " + err_code + "\n";
    send(client_sock, msg.c_str(), msg.length(), MSG_NOSIGNAL);
}

// ========================
// 主函数
// ========================
int main() {
    const int PORT = 8888;
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        std::cout << "Client connected from "
                  << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;

        volatile bool send_camera_rt = false;
        volatile bool send_th_rt = false;
        volatile bool send_fan_rt = false;
        volatile bool send_ma_rt = false;

        char buffer[1024];
        while (1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(client_fd, &readfds);
            struct timeval tv = {0, 10000};

            if (send_camera_rt || send_th_rt || send_fan_rt || send_ma_rt) {
                int activity = select(client_fd + 1, &readfds, nullptr, nullptr, &tv);
                if (activity > 0 && FD_ISSET(client_fd, &readfds)) {
                    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                    if (bytes <= 0) break;
                    buffer[bytes] = '\0';
                } else {
                    if (send_camera_rt) {
                        std::vector<uint8_t> frame;
                        if (get_latest_camera_frame(frame)) {
                            uint32_t len = htonl(static_cast<uint32_t>(frame.size()));
                            send(client_fd, &len, sizeof(len), MSG_NOSIGNAL);
                            send(client_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
                        }
                    }
                    if (send_th_rt) {
                        std::string data;
                        if (get_latest_th_data(data)) {
                            send(client_fd, data.c_str(), data.size(), MSG_NOSIGNAL);
                            send(client_fd, "\n", 1, MSG_NOSIGNAL);
                        }
                    }
                    if (send_fan_rt) {
                        std::string data;
                        if (get_latest_fan_data(data)) {
                            send(client_fd, data.c_str(), data.size(), MSG_NOSIGNAL);
                            send(client_fd, "\n", 1, MSG_NOSIGNAL);
                        }
                    }
                    if (send_ma_rt) {
                        std::string data;
                        if (get_latest_ma_data(data)) {
                            send(client_fd, data.c_str(), data.size(), MSG_NOSIGNAL);
                            send(client_fd, "\n", 1, MSG_NOSIGNAL);
                        }
                    }
                    usleep(30000);
                    continue;
                }
            } else {
                ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0) break;
                buffer[bytes] = '\0';
            }

            std::string cmd(buffer);
            if (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
                cmd.pop_back();
                if (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
                    cmd.pop_back();
                }
            }
            if (cmd.empty()) continue;

            std::cout << "Received command: [" << cmd << "]" << std::endl;

            if (cmd.size() >= 4 && cmd.substr(0, 4) == "ERR ") {
                send_error(client_fd, cmd.substr(4));
                continue;
            }

            std::vector<std::string> parts = split(cmd, ' ');
            std::string dev = parts[0];
            std::string action = (parts.size() > 1) ? parts[1] : "";

            if (dev == "FA") {
                if (action == "On") {
                    g_fan_on = true;
                    pthread_t tid;
                    pthread_create(&tid, nullptr, fan_task, nullptr);
                    pthread_detach(tid);
                } else if (action == "Off") {
                    g_fan_on = false;
                } else if (action == "RT") {
                    send_fan_rt = true;
                }
            }
            else if (dev == "TH") {
                if (action == "On") {
                    g_th_on = true;
                    pthread_t tid;
                    pthread_create(&tid, nullptr, th_task, nullptr);
                    pthread_detach(tid);
                } else if (action == "Off") {
                    g_th_on = false;
                } else if (action == "RT") {
                    send_th_rt = true;
                }
            }
            else if (dev == "CM") {
                if (action == "On") {
                    g_camera_on = true;
                    pthread_t tid;
                    pthread_create(&tid, nullptr, camera_task, nullptr);
                    pthread_detach(tid);
                } else if (action == "Off") {
                    g_camera_on = false;
                } else if (action == "RT") {
                    send_camera_rt = true;
                } else if (action == "DATAEND" || action == "STOP") {
                    send_camera_rt = false;
                    send_th_rt = false;
                    send_fan_rt = false;
                    send_ma_rt = false;
                }
            }
            else if (dev == "MA") {
                if (action == "On") {
                    g_ma_on = true;
                    pthread_t tid;
                    pthread_create(&tid, nullptr, ma_task, nullptr);
                    pthread_detach(tid);
                } else if (action == "Off") {
                    g_ma_on = false;
                } else if (action == "RT") {
                    send_ma_rt = true;
                }
            }
            else {
                std::string msg = "UNKNOWN_COMMAND\n";
                send(client_fd, msg.c_str(), msg.length(), MSG_NOSIGNAL);
            }
        }

        close(client_fd);
        std::cout << "Client disconnected." << std::endl;
    }

    close(server_fd);
    return 0;
}