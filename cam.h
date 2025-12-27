// cam.h
#ifndef CAM_H
#define CAM_H

#include <sys/types.h>
#include <linux/videodev2.h>

#define REQBUFS_COUNT 4

struct cam_buf {
    void *start;
    size_t length;
};

// 全局缓冲区（由 cam.cpp 定义）
extern struct cam_buf bufs[REQBUFS_COUNT];

#ifdef __cplusplus
extern "C" {
#endif

int camera_init(char *devpath, unsigned int *width, unsigned int *height,
                unsigned int *size, unsigned int *ismjpeg);
int camera_start(int fd);
int camera_dqbuf(int fd, void **buf, unsigned int *size, unsigned int *index);
int camera_eqbuf(int fd, unsigned int index);
int camera_stop(int fd);
int camera_exit(int fd);

#ifdef __cplusplus
}
#endif

#endif // CAM_H