#include "serial.h"

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

/* Map an integer baud rate to the matching termios speed_t constant. */
static speed_t baud_to_speed(int baud)
{
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

/* True for device names we consider usable serial ports. */
static int is_serial_name(const char *name)
{
#if defined(__APPLE__)
    /* macOS: callout devices, e.g. cu.usbserial-XXXX, cu.usbmodemXXXX */
    return strncmp(name, "cu.", 3) == 0;
#else
    /* Linux: USB adapters, CDC-ACM, and on-board UARTs */
    return strncmp(name, "ttyUSB", 6) == 0 ||
           strncmp(name, "ttyACM", 6) == 0 ||
           strncmp(name, "ttyS",   4) == 0;
#endif
}

void serial_list_ports(serial_port_list *list)
{
    DIR *dir;
    struct dirent *ent;

    list->count = 0;
    dir = opendir("/dev");
    if (!dir)
        return;

    while ((ent = readdir(dir)) != NULL && list->count < SERIAL_MAX_PORTS) {
        if (!is_serial_name(ent->d_name))
            continue;
        snprintf(list->names[list->count], SERIAL_NAME_LEN,
                 "/dev/%.*s", (int)(SERIAL_NAME_LEN - sizeof("/dev/")),
                 ent->d_name);
        list->count++;
    }
    closedir(dir);
}

int serial_open(const char *path, int baud)
{
    int fd;
    struct termios tio;
    speed_t speed = baud_to_speed(baud);

    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    /* 8 data bits, no parity, 1 stop bit, no flow control */
    tio.c_cflag &= ~(unsigned long)(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cflag |= (CLOCAL | CREAD);
#ifdef CRTSCTS
    tio.c_cflag &= ~(unsigned long)CRTSCTS;
#endif

    /* fully non-blocking: read() returns immediately */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

void serial_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int serial_write(int fd, const char *data, size_t len)
{
    ssize_t n;
    if (fd < 0)
        return -1;
    n = write(fd, data, len);
    return (int)n;
}

int serial_read(int fd, char *buf, size_t max)
{
    ssize_t n;
    if (fd < 0)
        return -1;
    n = read(fd, buf, max);
    if (n < 0) {
        /* no data available right now is not an error for us */
        return 0;
    }
    return (int)n;
}
