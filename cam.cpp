// cam.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "cam.h"

// 内部结构，不对外暴露
#define REQBUFS_COUNT 4

struct cam_buf {
    void *start;
    size_t length;
};

// 静态全局变量：仅本文件可见
static struct cam_buf bufs[REQBUFS_COUNT];
static struct v4l2_requestbuffers reqbufs;

int camera_init(char *devpath, unsigned int *width, unsigned int *height,
                unsigned int *size, unsigned int *ismjpeg)
{
    if (!devpath || !width || !height || !size || !ismjpeg) {
        errno = EINVAL;
        return -1;
    }

    int fd = open(devpath, O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        perror("open camera device");
        return -1;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        close(fd);
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        close(fd);
        return -1;
    }

    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    // 尝试 MJPEG
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0) {
        *ismjpeg = 1;  // ✅ MJPEG 成功
        goto get_fmt;
    }

    // 尝试 YUYV
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0) {
        *ismjpeg = 0;  // ✅ YUYV 成功
        goto get_fmt;
    }

    fprintf(stderr, "Failed to set format (MJPEG or YUYV)\n");
    close(fd);
    return -1;

get_fmt:
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
        perror("VIDIOC_G_FMT");
        close(fd);
        return -1;
    }

    // 请求缓冲区
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = REQBUFS_COUNT;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbufs.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbufs) == -1) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return -1;
    }

    if (reqbufs.count < REQBUFS_COUNT) {
        fprintf(stderr, "Insufficient buffer memory\n");
        close(fd);
        return -1;
    }

    // 映射并入队所有缓冲区
    for (unsigned int i = 0; i < reqbufs.count; ++i) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            // 清理已映射的 buffer
            for (unsigned int j = 0; j < i; ++j) {
                munmap(bufs[j].start, bufs[j].length);
            }
            close(fd);
            return -1;
        }

        bufs[i].length = buf.length;
        bufs[i].start = mmap(nullptr, buf.length,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             fd, buf.m.offset);

        if (bufs[i].start == MAP_FAILED) {
            perror("mmap");
            for (unsigned int j = 0; j < i; ++j) {
                munmap(bufs[j].start, bufs[j].length);
            }
            close(fd);
            return -1;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            for (unsigned int j = 0; j <= i; ++j) {
                munmap(bufs[j].start, bufs[j].length);
            }
            close(fd);
            return -1;
        }
    }

    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;
    *size = bufs[0].length;

    return fd;
}

int camera_start(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    return 0;
}

int camera_dqbuf(int fd, void **buf, unsigned int *size, unsigned int *index)
{
    if (!buf || !size || !index) {
        errno = EINVAL;
        return -1;
    }

    fd_set fds;
    struct timeval timeout;
    struct v4l2_buffer vbuf;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &fds, nullptr, nullptr, &timeout);
        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("select in dqbuf");
            return -1;
        } else if (ret == 0) {
            fprintf(stderr, "dqbuf: timeout\n");
            return -1;
        }

        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &vbuf) == -1) {
            perror("VIDIOC_DQBUF");
            return -1;
        }

        *buf = bufs[vbuf.index].start;
        *size = vbuf.bytesused;
        *index = vbuf.index;
        return 0;
    }
}

int camera_eqbuf(int fd, unsigned int index)
{
    if (index >= REQBUFS_COUNT) {
        errno = EINVAL;
        return -1;
    }

    struct v4l2_buffer vbuf = {};
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    vbuf.index = index;

    if (ioctl(fd, VIDIOC_QBUF, &vbuf) == -1) {
        perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}

int camera_stop(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("VIDIOC_STREAMOFF");
        return -1;
    }
    return 0;
}

int camera_exit(int fd)
{
    // 尝试出队所有仍在队列中的缓冲区（非必须，但更干净）
    struct v4l2_buffer vbuf = {};
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;

    for (unsigned int i = 0; i < reqbufs.count; ++i) {
        vbuf.index = i;
        ioctl(fd, VIDIOC_DQBUF, &vbuf); // 忽略返回值
    }

    // 解除内存映射
    for (unsigned int i = 0; i < reqbufs.count; ++i) {
        if (bufs[i].start != MAP_FAILED) {
            munmap(bufs[i].start, bufs[i].length);
            bufs[i].start = MAP_FAILED;
        }
    }

    return close(fd);
}