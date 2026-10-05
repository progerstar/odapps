import binascii
import struct


def mad_crc8(data):
    """CRC-8 used by MAD (AN10787): poly 0x1D, init 0xC7, no reflection, no final XOR."""
    crc = 0xC7
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


class WriteError(RuntimeError):
    """The card refused a block / trailer write"""


class MADWriter():
    MAD_KEY = b'\xA0\xA1\xA2\xA3\xA4\xA5'
    NDEF_KEY = b'\xD3\xF7\xD3\xF7\xD3\xF7'
    NDEF_AID = 0xE103  # AN1305
    HOLDER_AID = 0x0004  # AN10787
    MAD_ACL = b'\x78\x77\x88\xC1'
    NDEF_ACL = b'\x7F\x07\x88\x40'
    FACTORY_TRAILER = b'\xFF' * 6 + b'\xFF\x07\x80\x69' + b'\xFF' * 6
    SECTOR_BYTES = 48  # 3 data blocks of 16 bytes
    HOLDER_TYPES = {
        'Surname': 0x00,
        'Name': 0x01,
        'Sex': 0x02,
        'Other': 0x03
    }

    def __init__(self, keyb=None, debug=False):
        self._dbg = debug
        self.blocks = [bytearray(16), bytearray(16), bytearray(MADWriter.MAD_KEY + MADWriter.MAD_ACL + (keyb or b'\xFF' * 6))]

    def _print(self, fmt, *args):
        if self._dbg:
            print('MAD: ' + fmt.format(*args))

    def format(self, api, key=None, sectors=16):
        """Reset all sector trailers to the factory state (data blocks are left untouched)"""
        if key:
            if not isinstance(key, (bytes, bytearray)):
                bkey = binascii.unhexlify(str(key).encode())
            else:
                bkey = key
        else:
            bkey = b'\xFF' * 6
        if len(bkey) != 6:
            raise ValueError('Classic key must be 6 bytes long')

        res = api._send('AT+KB' + binascii.hexlify(bkey).decode())
        if res[0] != 'OK':
            raise RuntimeError('Cannot set the mifare key B')
        for sector in range(0, sectors):
            self._print('Try read sector {} with B-key', sector)
            res = api._send('AT+R{}'.format(sector * 4 + 3))
            if len(res) == 2 and res[1] == 'OK' and res[0].startswith('+DATA'):
                trailer = binascii.unhexlify(res[0].split(':', 1)[1].encode())
                if trailer[6:10] == MADWriter.FACTORY_TRAILER[6:10]:
                    # Factory access bits: the sector was never touched and key B cannot rewrite such a trailer anyway
                    self._print('Sector {} is already in the factory state', sector)
                    continue
                res = api._send('AT+W{}:'.format(sector * 4 + 3) + binascii.hexlify(MADWriter.FACTORY_TRAILER).decode().upper())
                if res[0] == 'OK':
                    self._print('Sector {} keys reset to factory defaults', sector)
                else:
                    raise RuntimeError('Cannot write sector {}'.format(sector))
            else:
                raise RuntimeError('Cannot read sector {}'.format(sector))

    def get_sector(self, i):
        loc = i * 2
        return struct.unpack('<H', self.blocks[loc // 16][(loc % 16):(loc % 16) + 2])[0]

    def set_sector(self, i, aid):
        loc = i * 2
        self._print('Sector {} AID 0x{:04x}', i, aid)
        struct.pack_into('<H', self.blocks[loc // 16], loc % 16, aid)

    def set_key_b(self, key):
        if not isinstance(key, (bytes, bytearray)):
            key = binascii.unhexlify(str(key).encode())

        if len(key) != 6:
            raise ValueError('Key must be 6 bytes long')
        memoryview(self.blocks[2])[-6:] = key

    def owner_sector(self):
        return self.blocks[0][1] & 0x0F

    def set_owner_sector(self, no):
        self.blocks[0][1] = self.blocks[0][1] & 0xF0 | (no & 0x0F)

    def make_holder(self, params):
        """Returns holder data padded to the whole number of sectors (48 bytes each)"""
        ret = bytearray(MADWriter.SECTOR_BYTES)

        def pack_item(itype, ivalue):
            if not isinstance(ivalue, (bytes, bytearray)):
                ivalue = str(ivalue).encode('utf-8')
            if len(ivalue) > 62:
                raise ValueError('Holder item is too long')
            return bytes([(itype & 0x03) << 6 | (len(ivalue) + 1)]) + ivalue + b'\x00'

        count = 0
        for typ, val in params.items():
            if isinstance(typ, int):
                item = pack_item(0x03 if typ > 0x03 else typ, val)
            elif typ in MADWriter.HOLDER_TYPES:
                item = pack_item(MADWriter.HOLDER_TYPES[typ], val)
            else:
                item = pack_item(0x03, val)
            while (len(item) + count) > len(ret):
                ret.extend(b'\x00' * MADWriter.SECTOR_BYTES)
            ret[count:count + len(item)] = item
            count += len(item)
        return ret

    def make_crc(self):
        self.blocks[0][0] = mad_crc8(bytes(self.blocks[0][1:]) + bytes(self.blocks[1]))

    def write(self, api):
        self.make_crc()
        if self._dbg:
            print('MAD Data:\n\t1: {}\n\t2: {}\n\t3: {}'.format(binascii.hexlify(self.blocks[0]),
                                                                binascii.hexlify(self.blocks[1]), binascii.hexlify(self.blocks[2])))
        for i in range(3):
            res = api._send('AT+W{}:'.format(i + 1) + binascii.hexlify(self.blocks[i]).decode())
            if res[0] != 'OK':
                raise WriteError('Sector 0 block {} write failed'.format(i + 1))

    def write_ndef(self, api, data, keyb=None, holder=None, owner=None, start=None, sectors=16):
        """
        Writes NDEF data (+ optional holder / owner info) and finally the MAD.

        :param sectors: the number of sectors the card has (16 for 1K, 5 for Mini); MAD1 can address up to 16
        """
        # MAD1 only: sector 0 is the MAD itself, sectors 1..15 may carry data
        sectors = min(sectors, 16)
        holder_info = None
        owner_info = None
        if holder:
            holder_info = self.make_holder(holder)
        if owner:
            owner_info = self.make_holder(owner)

        total_sectors = (len(data) + MADWriter.SECTOR_BYTES - 1) // MADWriter.SECTOR_BYTES
        total_sectors += len(holder_info) // MADWriter.SECTOR_BYTES if holder_info else 0
        total_sectors += len(owner_info) // MADWriter.SECTOR_BYTES if owner_info else 0

        sector = 1 if start is None else int(start)
        if sector < 1:
            raise RuntimeError('Cannot write - sector 0 is reserved for the MAD')
        if (sector + total_sectors) > sectors:
            raise RuntimeError('Cannot write - will not fit (need {} sectors from sector {}, the card has {})'.format(total_sectors, sector, sectors))

        # B-key
        if keyb:
            if not isinstance(keyb, (bytes, bytearray)):
                bkey = binascii.unhexlify(str(keyb).encode())
            else:
                bkey = keyb
        else:
            bkey = bytes(self.blocks[2][-6:])

        ndef_keys = {
            'A': MADWriter.NDEF_KEY,
            'acl': MADWriter.NDEF_ACL,
            'B': bkey
        }
        mad_keys = {
            'A': MADWriter.MAD_KEY,
            'acl': MADWriter.MAD_ACL,
            'B': bkey
        }
        # Start with data
        self._print('Writing NDEF data for sector {}', sector)
        ndef_blocks = data if ((len(data) % MADWriter.SECTOR_BYTES) == 0) else (data + b'\x00' * (MADWriter.SECTOR_BYTES - (len(data) % MADWriter.SECTOR_BYTES)))
        sector = self._write_sectors(sector, ndef_blocks, MADWriter.NDEF_AID, ndef_keys, api)
        self._print('\tTo sector {}', sector - 1)
        if holder_info:
            # Make-holder produces x48 aligned bytearrays
            self._print('Writing holder data from sector {}', sector)
            sector = self._write_sectors(sector, holder_info, MADWriter.HOLDER_AID, mad_keys, api)
            self._print('\tTo sector {}', sector - 1)
        if owner_info:
            # Make-holder produces x48 aligned bytearrays
            self._print('Writing owner data from sector {}', sector)
            self.set_owner_sector(sector)
            sector = self._write_sectors(sector, owner_info, MADWriter.HOLDER_AID, mad_keys, api)
            self._print('\tTo sector {}', sector - 1)
        # MAD goes last - it is the "commit" of the whole operation
        self.write(api)

    def _trailer(self, akey, acl, bkey):
        return akey + acl + bkey

    def _write_sectors(self, sector, data, aid, keys, api):
        block_no = 0
        for i in range(0, len(data), 16):
            res = api._send('AT+W{}:'.format(4 * sector + block_no) + binascii.hexlify(data[i:i + 16]).decode())
            if res[0] != 'OK':
                raise WriteError('Sector {} block {} write failed'.format(sector, block_no))
            block_no += 1
            if block_no == 3:
                # Trailer
                res = api._send('AT+W{}:'.format(4 * sector + 3) + binascii.hexlify(self._trailer(keys['A'], keys['acl'], keys['B'])).decode())
                if res[0] != 'OK':
                    raise WriteError('Sector {} trailer write failed'.format(sector))
                self.set_sector(sector, aid)
                sector += 1
                block_no = 0
        return sector
