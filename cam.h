// cam.h
#ifndef CAM_H
#define CAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <linux/videodev2.h>

/**
 * @brief 初始化摄像头设备
 * @param devpath 设备路径，如 "/dev/video0"
 * @param width 输入期望宽度，输出实际宽度
 * @param height 输入期望高度，输出实际高度
 * @param size 输出每帧缓冲区大小（字节）
 * @param ismjpeg 输出：1 表示使用 MJPEG，0 表示 YUYV
 * @return 文件描述符（>=0 成功），-1 失败
 */
int camera_init(char *devpath, unsigned int *width, unsigned int *height,
                unsigned int *size, unsigned int *ismjpeg);

/**
 * @brief 启动视频流
 */
int camera_start(int fd);

/**
 * @brief 出队一帧图像（阻塞，带超时）
 * @param buf 输出：指向图像数据的指针（来自 mmap）
 * @param size 输出：实际有效字节数（bytesused）
 * @param index 输出：缓冲区索引（用于后续 eqbuf）
 */
int camera_dqbuf(int fd, void **buf, unsigned int *size, unsigned int *index);

/**
 * @brief 将缓冲区重新入队，供摄像头继续填充
 */
int camera_eqbuf(int fd, unsigned int index);

/**
 * @brief 停止视频流
 */
int camera_stop(int fd);

/**
 * @brief 释放所有资源并关闭设备
 */
int camera_exit(int fd);

#ifdef __cplusplus
}
#endif

#endif // CAM_H