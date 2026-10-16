/*
 * serial.h - minimal cross-platform (POSIX) serial port helper.
 *
 * Replaces the Qt QSerialPort / SerialThread machinery from the original
 * gpio_extender project. The Nuklear UI runs a single-threaded immediate
 * mode loop, so instead of a worker thread we keep the port open and do
 * non-blocking reads every frame.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

#define SERIAL_MAX_PORTS 64
#define SERIAL_NAME_LEN  256

typedef struct {
    char names[SERIAL_MAX_PORTS][SERIAL_NAME_LEN];
    int  count;
} serial_port_list;

/* Enumerate available serial devices (e.g. /dev/cu.* on macOS,
 * /dev/ttyUSB*, /dev/ttyACM* on Linux). */
void serial_list_ports(serial_port_list *list);

/* Open the device at `path` with the given baud rate, 8N1, raw, non-blocking.
 * Returns a file descriptor (>= 0) on success, or -1 on failure. */
int serial_open(const char *path, int baud);

/* Close a port opened with serial_open(). */
void serial_close(int fd);

/* Write `len` bytes. Returns number of bytes written or -1 on error. */
int serial_write(int fd, const char *data, size_t len);

/* Non-blocking read of up to `max` bytes into `buf`.
 * Returns number of bytes read (0 if none available), or -1 on error. */
int serial_read(int fd, char *buf, size_t max);

#endif /* SERIAL_H */
