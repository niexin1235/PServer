
#ifndef _SERIAL_H
#define _SERIAL_H
/*------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include <unistd.h>     /*Unix 标准函数定义*/  
#include <fcntl.h>      /*文件控制定义*/  
#include <termios.h>    /*PPSIX 终端控制定义*/  
#include <errno.h>      /*错误号定义*/  

#include <sys/types.h>   
#include <sys/stat.h>     
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <assert.h>
#include <poll.h>
#include <linux/videodev2.h>


int serial_Open(char *devpath);  
int serial_Set(int fd,int speed,int flow_ctrl,int databits,int stopbits,int parity);  
int serial_init(char *devpath, int baudrate);  
ssize_t serial_recv_exact_nbytes(int fd, void *buf, size_t count);
ssize_t serial_send_exact_nbytes(int fd,  unsigned char *buf, size_t count);
int serial_exit(int fd);
#endif
