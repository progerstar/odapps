/*
 * serial_win32.c - Win32 implementation of the serial helper in serial.h.
 *
 * The POSIX backend (serial.c) uses termios + /dev enumeration, which native
 * Windows does not provide. This file implements the same serial.h interface
 * with the Win32 API:
 *
 *   - ports are opened as "\\.\COMx" via CreateFile + DCB + COMMTIMEOUTS,
 *   - reads are made fully non-blocking (return immediately with whatever is
 *     buffered) to match the POSIX VMIN=0/VTIME=0 behaviour main.c expects,
 *   - ports are enumerated from HKLM\HARDWARE\DEVICEMAP\SERIALCOMM.
 *
 * serial.h hands out an int "fd" and main.c treats fd < 0 as "closed". A
 * Win32 HANDLE is a pointer, so we keep an indexed table of open handles and
 * return the table index as the fd.
 */
#include "serial.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SERIAL_MAX_HANDLES 8

static HANDLE g_handles[SERIAL_MAX_HANDLES];
static int    g_init;

static void ensure_init(void)
{
    int i;
    if (g_init)
        return;
    for (i = 0; i < SERIAL_MAX_HANDLES; i++)
        g_handles[i] = INVALID_HANDLE_VALUE;
    g_init = 1;
}

/* Enumerate COM ports from the SERIALCOMM device map. Each value's data is
 * the friendly port name (e.g. "COM3"); the value name is the kernel device
 * path, which we ignore. */
void serial_list_ports(serial_port_list *list)
{
    HKEY key;
    DWORD i = 0;

    list->count = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return;

    for (;;) {
        char  valname[256];
        BYTE  data[256];
        DWORD vlen = sizeof(valname);
        DWORD dlen = sizeof(data);
        DWORD type = 0;
        LONG  r;

        r = RegEnumValueA(key, i, valname, &vlen, NULL, &type, data, &dlen);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        i++;
        if (r != ERROR_SUCCESS || type != REG_SZ)
            continue;
        if (list->count >= SERIAL_MAX_PORTS)
            break;

        /* registry strings aren't guaranteed null-terminated: force it */
        if (dlen >= sizeof(data))
            dlen = sizeof(data) - 1;
        data[dlen] = 0;

        snprintf(list->names[list->count], SERIAL_NAME_LEN, "%s",
                 (const char *)data);
        list->count++;
    }

    RegCloseKey(key);
}

int serial_open(const char *path, int baud)
{
    char         devpath[SERIAL_NAME_LEN + 8];
    HANDLE       h;
    DCB          dcb;
    COMMTIMEOUTS to;
    int          slot;

    ensure_init();

    for (slot = 0; slot < SERIAL_MAX_HANDLES; slot++)
        if (g_handles[slot] == INVALID_HANDLE_VALUE)
            break;
    if (slot == SERIAL_MAX_HANDLES)
        return -1;

    /* The "\\.\" prefix is required to open COM10 and above. */
    if (strncmp(path, "\\\\.\\", 4) == 0)
        snprintf(devpath, sizeof(devpath), "%s", path);
    else
        snprintf(devpath, sizeof(devpath), "\\\\.\\%s", path);

    h = CreateFileA(devpath, GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return -1;
    }

    /* 8 data bits, no parity, 1 stop bit, no flow control */
    dcb.BaudRate        = (DWORD)baud;
    dcb.ByteSize        = 8;
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = ONESTOPBIT;
    dcb.fBinary         = TRUE;
    dcb.fParity         = FALSE;
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDtrControl     = DTR_CONTROL_DISABLE;
    dcb.fRtsControl     = RTS_CONTROL_DISABLE;
    dcb.fOutX           = FALSE;
    dcb.fInX            = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return -1;
    }

    /* Fully non-blocking reads: ReadIntervalTimeout = MAXDWORD with both
     * total-timeout fields 0 makes ReadFile return at once with whatever is
     * already buffered (mirrors the POSIX VMIN=0/VTIME=0 setup). */
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 0;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    g_handles[slot] = h;
    return slot;
}

void serial_close(int fd)
{
    ensure_init();
    if (fd < 0 || fd >= SERIAL_MAX_HANDLES)
        return;
    if (g_handles[fd] != INVALID_HANDLE_VALUE) {
        CloseHandle(g_handles[fd]);
        g_handles[fd] = INVALID_HANDLE_VALUE;
    }
}

int serial_write(int fd, const char *data, size_t len)
{
    HANDLE h;
    DWORD  written = 0;

    ensure_init();
    if (fd < 0 || fd >= SERIAL_MAX_HANDLES)
        return -1;
    h = g_handles[fd];
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    if (!WriteFile(h, data, (DWORD)len, &written, NULL))
        return -1;
    return (int)written;
}

int serial_read(int fd, char *buf, size_t max)
{
    HANDLE h;
    DWORD  got = 0;

    ensure_init();
    if (fd < 0 || fd >= SERIAL_MAX_HANDLES)
        return -1;
    h = g_handles[fd];
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    /* no data available right now is not an error for us */
    if (!ReadFile(h, buf, (DWORD)max, &got, NULL))
        return 0;
    return (int)got;
}
