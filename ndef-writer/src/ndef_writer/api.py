"""ODRFID reader (CDC AT interface) and the NDEF layout of Type 2 and Mifare Classic tags."""
import binascii
import struct
import time

from .mad import MADWriter, WriteError


# Ref: NXP AN1303

CC_BYTE0_MAGIC = 0xE1
CC_BYTE1_VERSION = 0x10
CC_BYTE3_ACCESS = 0x00  # RW

TLV_TAG_NULL = 0x00
TLV_TAG_NDEF_MESSAGE = 0x03
TLV_TAG_PROPRIETARY = 0xFD
TLV_TAG_TERMINATOR = 0xFE


def CC_BYTE2_SIZE(user_size):
    return (user_size) // 8


def make_cc(user_size):
    return bytes([CC_BYTE0_MAGIC, CC_BYTE1_VERSION, CC_BYTE2_SIZE(user_size), CC_BYTE3_ACCESS])


def make_tlv(tag, data):
    if len(data) > 65534:
        raise ValueError('data too long')
    elif len(data) > 254:
        return bytes([tag, 0xFF]) + struct.pack('>H', len(data)) + data
    else:
        return bytes([tag, len(data)]) + data


class ODRFID_API():
    TYPES = {
        0: 'Mifare Classic 1K',
        1: 'Mifare Classic 4K',
        2: 'Mifare Classic Mini',
        3: 'Mifare Ultralight',
        5: 'Mifare Ultralight EV1 (80)',
        6: 'Mifare Ultralight EV1 (164)',
        7: 'Mifare PlusS 2K (SL1)',
        8: 'Mifare PlusS 4K (SL1)',
        12: 'Mifare Classic 2K',
        13: 'Mifare PlusX 2K (SL1)',
        14: 'Mifare PlusX 4K (SL1)',
        23: 'NTAG-213',
        24: 'NTAG-215',
        25: 'NTAG-216',
    }
    # Types the firmware reads / writes as Classic (see MX_RFID_IsClassic)
    CLASSIC_TYPES = (0, 1, 2, 7, 8, 12, 13, 14)
    # Types the firmware reads / writes as Ultralight (see MX_RFID_IsPlainUltralight / MX_RFID_IsUltralightEV1)
    UL_TYPES = (3, 5, 6, 23, 24, 25)
    # NDEF capacity in bytes (the value the CC of a factory tag declares: NTAG215 - 0x3E, NTAG216 - 0x6D)
    UL_TAG_SIZES = {3: 12 * 4, 5: 12 * 4, 6: 36 * 4, 23: 36 * 4, 24: 496, 25: 872}
    # Upper limit for the wait of the reply (seconds)
    REPLY_TIMEOUT = 3.0

    def __init__(self, port, debug=False):
        self.tty = port
        self._cl_offset = None
        self._dbg = debug

    def _print(self, fmt, *args):
        if self._dbg:
            print('API: ' + fmt.format(*args))

    def _read_reply(self, expected):
        """Reads until `expected` result codes (OK / ERROR) are seen or the timeout is over"""
        reply = b''
        deadline = time.monotonic() + self.REPLY_TIMEOUT
        while time.monotonic() < deadline:
            chunk = self.tty.read(max(1, getattr(self.tty, 'in_waiting', 0) or 0))
            if not chunk:
                continue
            reply += chunk
            lines = reply.decode('latin1').split('\r\n')
            if sum(1 for line in lines if line in ('OK', 'ERROR')) >= expected:
                break
        return reply

    def _send(self, data, reply=True):
        count = 0
        if reply and hasattr(self.tty, 'reset_input_buffer'):
            # Whatever is still in the buffer belongs to nobody: it would be taken for the reply
            self.tty.reset_input_buffer()
        if data:
            if isinstance(data, (list, set, tuple)):
                for i in data:
                    self.tty.write(i.encode('latin1'))
                    self.tty.write(b'\r')
                    count += 1
            else:
                self.tty.write(data.encode('latin1'))
                self.tty.write(b'\r')
                count = 1
            self.tty.flush()
            self._print('TX>> {}', data)
        if reply:
            raw = self._read_reply(count or 1)
            self._print('RX<< {}', raw)
            lines = list(filter(None, raw.strip().decode('latin1').split('\r\n')))
            if not lines:
                raise RuntimeError('No reply from the reader (command: {})'.format(
                    ', '.join(data) if isinstance(data, (list, tuple)) else data))
            return lines
        return []

    SIGNATURES = ('Open Development', 'Open-Development', 'ODRFID')

    def scanoff(self):
        info = []
        for attempt in range(2):
            info = self._send(('ATH', 'ATI'))
            # OK, Info, S/N, OK
            if any(sig in line for line in info for sig in ODRFID_API.SIGNATURES):
                break
            self._print('Unexpected reply to ATI: {}', info)
            time.sleep(0.3)  # let a late / foreign reply arrive, it is dropped by the next _send
        else:
            raise RuntimeError('This is not an ODRFID reader (reply: {})'.format(info))

        res = self._send('AT+SCAN0')
        if res[-1] != 'OK':
            raise RuntimeError('Scan mode could not be disabled')

    def scan(self, nocheck=False):
        if not nocheck:
            self.scanoff()
        else:
            self._send('ATH')

        _ = input('Place the tag on the reader, press any key:')
        res = self._send('AT+i')
        if len(res) != 2:
            raise RuntimeError('No tag')

        res = self._send('AT+S')
        if len(res) != 2:
            raise RuntimeError('Could not aquire the tag info')

        tag = {}
        # +UID=...,BC=..,BS=..,T=..
        for item in res[0].split(','):
            if item.startswith('+UID='):
                tag['uid'] = item[5:]
            elif item.startswith('BC='):
                tag['blocks'] = int(item[3:])
            elif item.startswith('BS='):
                tag['size'] = int(item[3:])
            elif item.startswith('T='):
                tag['type'] = int(item[2:])

        if 'type' not in tag:
            raise RuntimeError('Could not determine the tag type')
        return tag

    @staticmethod
    def mfc_sectors(tag):
        # 1K - 16, Mini - 5; larger cards are used as 1K (MAD1 addresses 16 sectors only)
        return min(16, tag.get('blocks', 64) // 4)

    def format_mfc(self, keyb=None):
        tag = self.scan()
        if tag['type'] not in ODRFID_API.CLASSIC_TYPES:
            raise ValueError('Unsupported tag - Mifare Classic or compatible expected')

        MADWriter(debug=self._dbg).format(self, key=keyb, sectors=self.mfc_sectors(tag))

    def write_ndef(self, data, nocheck=False):
        tag = self.scan(nocheck=nocheck)

        print('Processing.... Do NOT remove the tag!')
        if tag['type'] in ODRFID_API.CLASSIC_TYPES:
            self.write_ndef_cl(tag, data)
        elif tag['type'] in ODRFID_API.UL_TYPES:
            self.write_ndef_ul(tag, data)
        else:
            raise ValueError('Tag type {} is not supported'.format(tag['type']))

    def write_ndef_cl(self, tag, data):
        # Currently only MAD1 is supported
        op = MADWriter(debug=self._dbg)
        # Use default key A - this is vital (some 1K card allow writing with key B, but actually do nothing)
        res = self._send('AT+KAFFFFFFFFFFFF')
        if res[0] != 'OK':
            raise RuntimeError('Cannot reset the mifare key')
        # AN1304 states that 0xFE tlv should not have length and value
        data_to_send = make_tlv(TLV_TAG_NDEF_MESSAGE, data) + bytes([TLV_TAG_TERMINATOR, ])  # make_tlv(TLV_TAG_TERMINATOR, b'')

        try:
            op.write_ndef(self, data_to_send, owner={'Name': 'Open Development LLC', 'Other': 'https://open-dev.ru'},
                          start=self._cl_offset, sectors=self.mfc_sectors(tag))
        except WriteError as err:
            raise RuntimeError('{}. A tag that already holds NDEF data (or has non-factory keys) must be reset '
                               'with the "format" command first'.format(err)) from err

    def _write_block(self, block, data):
        res = self._send('AT+W{}:{}'.format(block, binascii.hexlify(data).decode()))
        if res[0] != 'OK':
            raise RuntimeError('Failed to program block {}'.format(block))

    def write_ndef_ul(self, tag, data):
        capacity = ODRFID_API.UL_TAG_SIZES.get(tag['type'], 0)

        # CC
        res = self._send('AT+R3')
        if not res[0].startswith('+DATA 3:'):
            raise RuntimeError('Unexpected reply from the tag')
        cc = binascii.unhexlify(res[0][8:].encode())
        if cc == b'\x00\x00\x00\x00':
            # Needs programming (OTP: cannot be undone)
            cc = make_cc(capacity)
            self._write_block(3, cc)
        elif cc[0] != CC_BYTE0_MAGIC or (cc[1] >> 4) != (CC_BYTE1_VERSION >> 4) or cc[3] != CC_BYTE3_ACCESS:
            raise RuntimeError('Cannot write tag - capability container set to a different value ({})'.format(binascii.hexlify(cc)))
        else:
            # The tag itself declares how much may be used
            capacity = min(capacity, cc[2] * 8) if capacity else cc[2] * 8

        # AN1304 states that 0xFE tlv should not have length and value
        data_to_send = make_tlv(TLV_TAG_NDEF_MESSAGE, data) + bytes([TLV_TAG_TERMINATOR, ])  # make_tlv(TLV_TAG_TERMINATOR, b'')
        if len(data_to_send) > capacity:
            raise ValueError('NDEF message is too big - need {} bytes, have {} bytes'.format(len(data_to_send), capacity))

        self._print('To tag: {} ({} bytes)', binascii.hexlify(data_to_send), len(data_to_send))
        if len(data_to_send) % 4:
            # Pad to full block
            data_to_send += b'\x00' * (4 - (len(data_to_send) & 0x03))
        pages = [data_to_send[i:i + 4] for i in range(0, len(data_to_send), 4)]

        # NFC Forum T2T: never leave a valid length in front of a half-written message.
        # First make the tag "empty", then write the body and then the page with the real length.
        first_block = 4
        if len(pages) > 1:
            self._write_block(first_block, bytes([TLV_TAG_NDEF_MESSAGE, 0x00, TLV_TAG_TERMINATOR, 0x00]))
            for i in range(1, len(pages)):
                self._write_block(first_block + i, pages[i])
        self._write_block(first_block, pages[0])
