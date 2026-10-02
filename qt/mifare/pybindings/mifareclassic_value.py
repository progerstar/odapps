#!/usr/bin/python3

import serial
from binascii import hexlify, unhexlify
from struct import unpack
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

    def key(self, key, ktype='A'):
        self.write('K{}:{}'.format(ktype, key))
        self.readOK()
        return True

    def uid(self):
        return hexlify(self._tag['uid']).decode() if self._tag else None

    def tag(self):
        return self._tag['type'] if self._tag else 0xFF

    def value(self, block):
        self.write('R{}'.format(block))
        reply = self.read()
        if len(reply) and (reply[1] == 'OK'):
            blk_data = unhexlify(reply[0].split(':')[-1].encode())
            # 32le | ~32le | 32le | A | ~A | A | ~A
            (v1, v1rev, v2, a1, na1, a2, na2) = unpack('<iiiBBBB', blk_data)
            # print('Block {} value parts: {}, {}, {}, {}, {}, {}, {}'.format(block, v1, v1rev, v2, a1, na1, a2, na2))
            if (v1 != v2) or (~v1rev != v1) or (a1 != a2) or (a1 != (~na1 & 0xFF)) or (na1 != na2):
                raise ValueError('not a value block: check failed')
            return (v1, a1)

    def increment(self, block, delta):
        self.write('VI{}:{}'.format(block, delta))
        self.readOK()
        return True

    def decrement(self, block, delta):
        self.write('VD{}:{}'.format(block, delta))
        self.readOK()
        return True

    # Private
    def write(self, cmd):
        self._hnd.write('AT+{0}\r'.format(cmd).encode())

    def read(self):
        data = self._hnd.read(1024)
        return list(filter(bool, data.decode().split('\r\n')))

    def readOK(self):
        reply = self.read()
        if len(reply) != 1 or reply[0] != 'OK':
            raise RuntimeError('operation failed ({})'.format('inv' if not len(reply) else reply[0]))


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='/dev/ttyACM0', help='Serial port')
    parser.add_argument('--key', default='FFFFFFFFFFFF', help='Mifare Classic key (in hex)')
    parser.add_argument('--type', default='A', help='Mifare Classic key type (A or B)')

    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.4) as fd:
        reader = CDCReader(fd)
        it = 0
        while True:
            print('Scanning tags ' + ('-', '/', '-', '\\')[it & 0x03], end='\r')
            it += 1
            if reader.scan():
                break
            # sleep(1) - timeout does that

        if reader.tag() not in (0, 1, 7, 8, 13, 14):
            print('This tool is only for Classic compatible tags')
            sys.exit(1)

        useri = input('Value block number ("-1" to exit): ')
        try:
            BNr = int(useri)
        except:
            print('Not a number')
            sys.exit(1)

        while True:
            cval = reader.value(BNr)
            print('Block contents: value {0} (0x{0:X}), address {1} (0x{1:X})'.format(cval[0], cval[1]))
            useri = input('Action [I/D] ("q" to quit): ')
            if useri.strip() in ('i', 'I'):
                useri = input('Increment by: ')
                reader.increment(BNr, int(useri))
            elif useri.strip() in ('d', 'D'):
                useri = input('Decrement by: ')
                reader.decrement(BNr, int(useri))
            else:
                break
    sys.exit(0)
