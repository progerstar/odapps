#!/usr/bin/python3

import serial
from binascii import hexlify, unhexlify
from time import sleep
import sys


TAG_TYPES = {
    0: 'Mifare Classic 1K',
    1: 'Mifare Classic 4K',
    2: 'Mifare Classic Mini',
    3: 'Mifare Ultralight',
    4: 'Mifare Ultralight C',
    5: 'Mifare Ultralight EV1 (80 bytes)',
    6: 'Mifare Ultralight EV1 (164 bytes)',
    7: 'Mifare Plus S 2K (SL1)',
    8: 'Mifare Plus S 4K (SL1)',
    9: 'Mifare DESFire 2K',
    10: 'Mifare DESFire 4K',
    11: 'Mifare DESFire 8K',
    12: 'Mifare Classic 2K',
    13: 'Mifare Plus X 2K (SL1)',
    14: 'Mifare Plus X 4K (SL1)',
    15: 'Mifare Plus X (SL0)',
    16: 'Mifare Plus X 2K (SL2)',
    17: 'Mifare Plus X 4K (SL2)',
    18: 'Mifare Plus X (SL3)',
    19: 'Mifare Plus S (SL0)',
    22: 'Mifare Plus S (SL3)',
    23: 'Mifare NTAG 213',
    24: 'Mifare NTAG 215',
    25: 'Mifare NTAG 216',
    26: 'Mifare Ultralight Nano'
}


class CDCReader():

    def __init__(self, port):
        self._hnd = port
        self._tag = None
        self.write('SCAN0')
        self._hnd.flush()
        self.read()

    def info(self):
        self._hnd.write('ATI\r')
        reply = self.read()
        return reply[0] + ', ' + reply[1]

    def scan(self):
        self.write('i')
        reply = self.read()
        if len(reply) == 2:
            uid_sak = reply[0].split('=')[1]
            if len(uid_sak) in (2 * (4 + 1), 2 * (7 + 1), 2 * (10 + 1)):
                self._tag = {
                    'uid': unhexlify(uid_sak[:-2].encode()),
                    'sak': int(uid_sak[-2:], 16)
                }
                self.write('S')
                reply = self.read()
                if len(reply) == 2:
                    self._tag['type'] = int(reply[0].split(',')[-1][2:])
                    print('TAG found: {0} (sak {1}, {2})'.format(hexlify(self._tag['uid']), hex(self._tag['sak']), TAG_TYPES.get(self._tag['type'], 'Unknown')))
                    return True

    def uid(self):
        return hexlify(self._tag['uid']).decode() if self._tag else None

    def tag(self):
        return self._tag['type'] if self._tag else 0xFF

    def sl0write(self, block, data):
        self.write('E{}:{}'.format(block, data))
        reply = self.read()
        return len(reply) and (reply[0] == 'OK')

    def sl0commit(self):
        self.write('J1')
        reply = self.read()
        if not len(reply) or reply[0] != 'OK':
            print('Failed')
            return None
        print('Security Level has been changed to 1')
        if not self._tag:
            return True

        self.write('C={}'.format(hexlify(self._tag['uid'])))
        reply = self.read()
        if not len(reply) or reply[0] != 'OK':
            print('TAG removed')
            return True
        self.write('SELECT={}'.format(hexlify(self._tag['uid'])))
        reply = self.read()
        if len(reply) and reply[0] == 'OK':
            self.write('S')
            reply = self.read()
            if len(reply) == 2:
                self._tag['type'] = int(reply[1].split(',')[-1][2])
                print('TAG found: {0} (sak {1}, {2})'.format(hexlify(self._tag['uid']), hex(self._tag['sak']), TAG_TYPES.get(self._tag['type'], 'Unknown')))
                return True

    # Private
    def write(self, cmd):
        self._hnd.write('AT+{0}\r'.format(cmd).encode())

    def read(self):
        data = self._hnd.read(1024)
        return list(filter(bool, data.decode().split('\r\n')))


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='/dev/ttyACM0')

    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=1) as fd:
        reader = CDCReader(fd)
        it = 0
        while True:
            print('Scanning tags ' + ('-', '/', '-', '\\')[it & 0x03], end='\r')
            it += 1
            if reader.scan():
                break
            # sleep(1) - timeout does that

        if reader.tag() not in (15, 19):
            print('This tool is only for Plus SL0 tags')
            sys.exit(1)
        while True:
            useri = input('Key number ("q" to stop): ')
            if useri.strip() == 'q':
                break
            try:
                KeyBNr = int(useri)
            except ValueError:
                # try hex
                KeyBNr = int(useri, 16)
            except Exception as e:
                print('Error: {} ({})'.format(e, type(e)))
                continue

            useri = input('Data (32 hex digits - 16 bytes): ')
            try:
                udata = unhexlify(useri)
                if len(udata) != 16:
                    print('Error: not enough / excess data')
                    continue
            except Exception as e:
                print('Error: {} ({})'.format(e, type(e)))
                continue

            reader.sl0write(KeyBNr, useri)
        useri = input('Commit? (This is an IRREVERSIBLE operation) [y/N]:')
        if useri in ('y', 'Y'):
            reader.sl0commit()
