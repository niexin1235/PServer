// cam.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

// V4L2 是 C 接口，需 extern "C"
extern "C" {
#include <linux/videodev2.h>
}

#include "cam.h"

struct cam_buf bufs[REQBUFS_COUNT];
static struct v4l2_requestbuffers reqbufs;  // 改为 static，仅本文件使用

int camera_init(char *devpath, unsigned int *width, unsigned int *height,
                unsigned int *size, unsigned int *ismjpeg)
{
    int i, fd = -1, ret;
    struct v4l2_buffer vbuf;
    struct v4l2_format format;
    struct v4l2_capability capability;

    fd = open(devpath, O_RDWR);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    ret = ioctl(fd, VIDIOC_QUERYCAP, &capability);
    if (ret == -1) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return -1;
    }

    if (!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Device does not support video capture\n");
        close(fd);
        return -1;
    }
    if (!(capability.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        close(fd);
        return -1;
    }

    // Try MJPEG
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.width = *width;
    format.fmt.pix.height = *height;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &format) == 0) {
        *ismjpeg = 1;  // 注意：原代码这里逻辑反了！MJPEG 成功应设 ismjpeg=1
        goto get_fmt;
    }

    // Try YUYV
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.width = *width;
    format.fmt.pix.height = *height;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &format) == 0) {
        *ismjpeg = 0;
        goto get_fmt;
    }

    perror("Failed to set format (MJPEG or YUYV)");
    close(fd);
    return -1;

get_fmt:
    if (ioctl(fd, VIDIOC_G_FMT, &format) == -1) {
        perror("VIDIOC_G_FMT");
        close(fd);
        return -1;
    }

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = REQBUFS_COUNT;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbufs.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbufs) == -1) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return -1;
    }

    for (i = 0; i < (int)reqbufs.count; i++) {
        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        vbuf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &vbuf) == -1) {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return -1;
        }

        bufs[i].length = vbuf.length;
        bufs[i].start = mmap(NULL, vbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, vbuf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return -1;
        }

        if (ioctl(fd, VIDIOC_QBUF, &vbuf) == -1) {
            perror("VIDIOC_QBUF");
            close(fd);
            return -1;
        }
    }

    *width = format.fmt.pix.width;
    *height = format.fmt.pix.height;
    *size = bufs[0].length;

    return fd;
}

int camera_start(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return (ioctl(fd, VIDIOC_STREAMON, &type) == -1) ? -1 : 0;
}

int camera_dqbuf(int fd, void **buf, unsigned int *size, unsigned int *index)
{
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
            fprintf(stderr, "dqbuf timeout\n");
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
    struct v4l2_buffer vbuf;
    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    vbuf.index = index;
    return (ioctl(fd, VIDIOC_QBUF, &vbuf) == -1) ? -1 : 0;
}

int camera_stop(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) ? -1 : 0;
}

int camera_exit(int fd)
{
    struct v4l2_buffer vbuf;
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;

    // 尝试清空队列（非必须，但安全）
    for (int i = 0; i < (int)reqbufs.count; i++) {
        ioctl(fd, VIDIOC_DQBUF, &vbuf);
    }

    for (int i = 0; i < (int)reqbufs.count; i++) {
        if (bufs[i].start != MAP_FAILED)
            munmap(bufs[i].start, bufs[i].length);
    }

    return close(fd);
}