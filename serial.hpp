#ifndef _SERIAL_H_
#define _SERIAL_H_

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

#define BAUDRATE B9600//B115200//设置条码扫描仪的串口通讯波特率为115200BPS
#define COM2 "/dev/ttyUSB0"

class MySerial {
public:
    MySerial();

    ~MySerial();

public:
    static int set_opt(int fd, int nSpeed, int nBits, char nEvent, int nStop);

    static int open_port(int comport);

    static int nwrite(int serialfd, const char *data, int datalength);

    static void nread(int fd, void *data, int datalength);
};

#endif


