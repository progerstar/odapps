"""
Tests of ndef-writer that need neither a reader nor the Qt/firmware code.

The reader is modelled after the AT interface of the ODRFID firmware:
ATH, ATI, AT+SCAN0, AT+i, AT+S, AT+R<n>, AT+W<n>:<hex>, AT+KA/KB<12 hex>.
"""
import io
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

import ndef_writer  # noqa: E402
from ndef_writer import api as apimod, cli, mad  # noqa: E402

try:
    import ndef
except ImportError:
    ndef = None

FACTORY_KEY = b"\xFF" * 6


# ---------------------------------------------------------------- cards ---

class ClassicCard:
    """Mifare Classic 1K / Mini with real access-bit handling (key A / key B per block)"""

    # (C1, C2, C3) -> keys allowed to write
    DATA_WRITE = {(0, 0, 0): "AB", (1, 0, 0): "B", (1, 1, 0): "B", (0, 1, 1): "B"}
    TRAILER_KEYA_WRITE = {(0, 0, 0): "A", (0, 0, 1): "A", (1, 0, 0): "B", (0, 1, 1): "B"}
    TRAILER_ACL_WRITE = {(0, 0, 1): "A", (0, 1, 1): "B", (1, 0, 1): "B"}
    TRAILER_KEYB_WRITE = TRAILER_KEYA_WRITE

    def __init__(self, sectors=16, type_id=0):
        self.type_id = type_id
        self.sectors = sectors
        self.blocks = [bytearray(16) for _ in range(sectors * 4)]
        self.blocks[0][:] = bytes.fromhex("01020304040800000000000000000000")
        for s in range(sectors):
            self.blocks[s * 4 + 3][:] = FACTORY_KEY + bytes.fromhex("FF078069") + FACTORY_KEY
        self.log = []
        self.fail_blocks = set()

    block_size = 16

    @property
    def block_count(self):
        return len(self.blocks)

    def info(self):
        return "+UID=01020304" + "08"

    @staticmethod
    def acl(trailer, block):
        b6, b7, b8 = trailer[6], trailer[7], trailer[8]
        c1, c2, c3 = (b7 >> 4) & 0xF, b8 & 0xF, (b8 >> 4) & 0xF
        assert (~c1 & 0xF) == (b6 & 0xF) and (~c2 & 0xF) == (b6 >> 4) and (~c3 & 0xF) == (b7 & 0xF), \
            "invalid access bits {} - the sector would be bricked".format(bytes(trailer).hex())
        bit = block % 4
        return (c1 >> bit) & 1, (c2 >> bit) & 1, (c3 >> bit) & 1

    def key_ok(self, sector, ktype, key):
        trailer = self.blocks[sector * 4 + 3]
        return bytes(trailer[0:6] if ktype == "A" else trailer[10:16]) == key

    def read(self, block, session):
        if block == self.sectors * 4:
            return None
        data = bytearray(self.blocks[block])
        if block % 4 == 3:
            data[0:6] = b"\x00" * 6
        return bytes(data)

    def write(self, block, data, session):
        if block in self.fail_blocks:
            return False
        sector, ktype = block // 4, session
        if block == 0:
            return False
        trailer = self.blocks[sector * 4 + 3]
        if block % 4 == 3:
            new = bytearray(data)
            self.acl(new, 3)  # validates the new access bits
            cond = self.acl(trailer, 3)
            if not all(ktype in table.get(cond, "") for table in
                       (self.TRAILER_KEYA_WRITE, self.TRAILER_ACL_WRITE, self.TRAILER_KEYB_WRITE)):
                return False
        else:
            if ktype not in self.DATA_WRITE.get(self.acl(trailer, block % 4), ""):
                return False
        self.blocks[block][:] = data
        self.log.append(block)
        return True

    # --- helpers for assertions -------------------------------------------
    def mad_blocks(self):
        return bytes(self.blocks[1]) + bytes(self.blocks[2])

    def sector_aid(self, sector):
        mb = self.mad_blocks()
        return struct.unpack_from("<H", mb, sector * 2)[0]

    def sector_data(self, sector):
        return b"".join(bytes(self.blocks[sector * 4 + i]) for i in range(3))


class UltralightCard:
    """Ultralight / NTAG (4-byte pages, CC in page 3 is OTP)"""

    block_size = 4

    def __init__(self, type_id, pages, cc=b"\x00\x00\x00\x00"):
        self.type_id = type_id
        self.pages = [bytearray(4) for _ in range(pages)]
        self.pages[3][:] = cc
        self.log = []
        self.fail_after = None

    @property
    def block_count(self):
        return len(self.pages)

    def info(self):
        return "+UID=04112233445566" + "00"

    def read(self, block, session):
        return bytes(self.pages[block])

    def write(self, block, data, session):
        if self.fail_after is not None and len(self.log) >= self.fail_after:
            return False
        if block < 3:
            return False
        if block == 3:
            data = bytes(a | b for a, b in zip(self.pages[3], data))  # OTP
        self.pages[block][:] = data
        self.log.append(block)
        return True

    def ndef_state(self):
        """What an NFC Forum reader would see: payload bytes, or None for a malformed memory"""
        mem = b"".join(bytes(p) for p in self.pages[4:])
        i = 0
        while i < len(mem) and mem[i] == 0x00:
            i += 1
        if i >= len(mem) or mem[i] != 0x03:
            return None
        i += 1
        length = mem[i]
        i += 1
        if length == 0xFF:
            length = struct.unpack_from(">H", mem, i)[0]
            i += 2
        payload = mem[i:i + length]
        if len(payload) != length or mem[i + length:i + length + 1] != b"\xFE":
            return None
        return payload


# --------------------------------------------------------------- reader ---

class FakeReader:
    """Serial-port-like object that answers like the ODRFID CDC AT interface"""

    def __init__(self, card, text="Open Development RFID Reader 3.14m 0123456789ab"):
        self.card = card
        self.text = text
        self.rx = bytearray()
        self.tx = bytearray()
        self.key_type = "A"
        self.key_a = FACTORY_KEY
        self.key_b = FACTORY_KEY
        self.session = None      # key type of the current authentication
        self.auth_sector = None
        self.halted = False
        self.commands = []

    in_waiting = property(lambda self: len(self.tx))

    def write(self, data):
        self.rx += data
        while b"\r" in self.rx:
            line, _, rest = bytes(self.rx).partition(b"\r")
            self.rx = bytearray(rest)
            self.handle(line.decode("latin1"))

    def flush(self):
        pass

    def reset_input_buffer(self):
        self.tx.clear()

    def read(self, size=1):
        out = bytes(self.tx[:size])
        del self.tx[:size]
        return out

    # AT_CMD output helpers: every item is surrounded by CR LF
    def item(self, text):
        self.tx += b"\r\n" + text.encode("latin1") + b"\r\n"

    def ok(self):
        self.item("OK")

    def error(self, cme=None):
        if cme:
            self.item("+CME ERROR: " + str(cme))
        self.item("ERROR")
        return

    def fail_card(self):
        self.halted = True
        self.auth_sector = None
        self.error(3)

    def authenticate(self, block):
        sector = block // 4
        if self.auth_sector == sector:
            return True
        if isinstance(self.card, UltralightCard):
            self.auth_sector = sector
            return True
        key = self.key_a if self.key_type == "A" else self.key_b
        if not self.card.key_ok(sector, self.key_type, key):
            return False
        self.auth_sector, self.session = sector, self.key_type
        return True

    def handle(self, cmd):
        self.commands.append(cmd)
        card = self.card
        if cmd == "ATH":
            self.tx.clear()
            self.ok()
        elif cmd == "ATI":
            self.item(self.text)
            self.item("S/N 0123456789AB")
            self.ok()
        elif cmd == "AT+SCAN0":
            self.ok()
        elif cmd == "AT+i":
            self.halted = False
            self.auth_sector = None
            self.item(card.info())
            self.ok()
        elif cmd == "AT+S":
            self.item("{},BC={},BS={},T={}".format(card.info(), card.block_count, card.block_size, card.type_id))
            self.ok()
        elif cmd.startswith("AT+K") and cmd[4] in "AB" and len(cmd) == 5 + 12:
            key = bytes.fromhex(cmd[5:])
            if cmd[4] == "A":
                self.key_type, self.key_a = "A", key
            else:
                self.key_type, self.key_b = "B", key
            self.ok()
        elif cmd.startswith("AT+R") and cmd[4:].isdigit():
            block = int(cmd[4:])
            if self.halted or block >= card.block_count or not self.authenticate(block):
                return self.fail_card()
            data = card.read(block, self.session)
            self.item("+DATA {}:{}".format(block, data.hex().upper()))
            self.ok()
        elif cmd.startswith("AT+W") and ":" in cmd and len(cmd) >= 11:
            num, _, hexdata = cmd[4:].partition(":")
            if not num.isdigit():
                return self.error()
            block = int(num)
            if block >= card.block_count or (len(hexdata) != card.block_size * 2 and len(hexdata) != 32):
                return self.error()
            if self.halted or not self.authenticate(block):
                return self.fail_card()
            data = bytes.fromhex(hexdata)
            if (len(data) == 4) != isinstance(card, UltralightCard):
                return self.error()
            if not card.write(block, data, self.session):
                return self.fail_card()
            self.ok()
        else:
            self.error()


def make_api(card):
    reader = FakeReader(card)
    api = apimod.ODRFID_API(reader)
    api.REPLY_TIMEOUT = 0.05
    return api, reader


def run(api, func, *args, **kwargs):
    with mock.patch("builtins.input", return_value=""), mock.patch("builtins.print"):
        return func(*args, **kwargs)


def ndef_message(payload_len):
    """A short NDEF message (MB|ME|SR record, type 'T'), `payload_len` bytes of payload"""
    payload = (b"\x02en" + bytes([0x41 + i % 26 for i in range(payload_len - 3)]))
    return bytes([0xD1, 0x01, len(payload) if payload_len < 256 else 0, ord("T")]) + payload


# ---------------------------------------------------------------- tests ---

class MadCrcTest(unittest.TestCase):
    def test_catalogue_check_value(self):
        # CRC-8/MIFARE-MAD
        self.assertEqual(mad.mad_crc8(b"123456789"), 0x99)

    def test_nxp_formatted_card(self):
        # Factory NFC-formatted 1K: info byte 0x01, all 15 sectors are 0xE103 -> CRC 0x14
        blocks = [bytearray(16), bytearray(16)]
        writer = mad.MADWriter()
        writer.blocks[:2] = blocks
        writer.set_owner_sector(1)
        for sector in range(1, 16):
            writer.set_sector(sector, 0xE103)
        writer.make_crc()
        self.assertEqual(bytes(writer.blocks[0][:4]).hex().upper(), "140103E1")
        self.assertEqual(writer.get_sector(5), 0xE103)  # an int, not a tuple

    def test_holder_longer_than_one_sector(self):
        writer = mad.MADWriter()
        data = writer.make_holder({"Name": "x" * 40, "Surname": "y" * 62, "Other": "z" * 62})
        self.assertEqual(len(data) % 48, 0)
        self.assertGreaterEqual(len(data), 40 + 62 + 62 + 6)


class UltralightTest(unittest.TestCase):
    def test_blank_ultralight_gets_cc_and_message(self):
        card = UltralightCard(3, 16)
        api, _ = make_api(card)
        msg = ndef_message(10)
        run(api, api.write_ndef, msg)
        self.assertEqual(bytes(card.pages[3]), bytes.fromhex("E1100600"))
        self.assertEqual(card.ndef_state(), msg)

    def test_ntag215_cc_is_the_datasheet_value(self):
        card = UltralightCard(24, 135)
        api, _ = make_api(card)
        run(api, api.write_ndef, ndef_message(10))
        self.assertEqual(bytes(card.pages[3]).hex(), "e1103e00")

    def test_ntag216_long_message_uses_three_byte_length(self):
        card = UltralightCard(25, 231, cc=bytes.fromhex("E1106D00"))
        api, _ = make_api(card)
        msg = ndef_message(300)
        self.assertGreater(len(msg), 255)
        run(api, api.write_ndef, msg)
        self.assertEqual(card.ndef_state(), msg)

    def test_too_big_message_is_rejected_before_any_write(self):
        card = UltralightCard(23, 45, cc=bytes.fromhex("E1101200"))
        api, _ = make_api(card)
        with self.assertRaisesRegex(ValueError, "too big"):
            run(api, api.write_ndef, ndef_message(150))
        self.assertEqual(card.log, [])

    def test_cc_declared_size_is_respected(self):
        card = UltralightCard(25, 231, cc=bytes.fromhex("E1100600"))  # NTAG216 limited to 48 bytes
        api, _ = make_api(card)
        with self.assertRaisesRegex(ValueError, "too big"):
            run(api, api.write_ndef, ndef_message(60))

    def test_foreign_cc_is_rejected(self):
        card = UltralightCard(23, 45, cc=bytes.fromhex("E1101280"))  # read-only
        api, _ = make_api(card)
        with self.assertRaisesRegex(RuntimeError, "capability container"):
            run(api, api.write_ndef, ndef_message(10))
        self.assertEqual(card.log, [])

    def test_tag_never_shows_a_half_written_message(self):
        old, new = ndef_message(12), ndef_message(100)
        # how many writes does a full run take?
        probe = UltralightCard(23, 45, cc=bytes.fromhex("E1101200"))
        api, _ = make_api(probe)
        run(api, api.write_ndef, new)
        total = len(probe.log)
        self.assertGreater(total, 3)

        for cut in range(total + 1):
            card = UltralightCard(23, 45, cc=bytes.fromhex("E1101200"))
            api, _ = make_api(card)
            run(api, api.write_ndef, old)
            card.log.clear()
            card.fail_after = cut
            try:
                run(api, api.write_ndef, new)
            except RuntimeError:
                pass
            self.assertIn(card.ndef_state(), (old, b"", new), "torn after {} writes".format(cut))
        self.assertEqual(card.ndef_state(), new)


class ClassicTest(unittest.TestCase):
    def check_card(self, card, msg, owner_sector):
        """Reads the card like an NFC Forum reader: through the MAD, with the public keys"""
        mb = card.mad_blocks()
        header = bytes(card.blocks[1])[:2]
        self.assertEqual(mad.mad_crc8(mb[1:]), header[0], "MAD CRC")
        self.assertEqual(header[1] & 0x3F, owner_sector)
        tlv = b""
        for sector in range(1, card.sectors):
            if card.sector_aid(sector) == 0xE103:
                trailer = card.blocks[sector * 4 + 3]
                self.assertEqual(bytes(trailer[:6]), mad.MADWriter.NDEF_KEY)
                tlv += card.sector_data(sector)
        self.assertEqual(tlv[0], 0x03)
        length = tlv[1]
        self.assertEqual(tlv[2:2 + length], msg)
        self.assertEqual(tlv[2 + length], 0xFE)
        # MAD sector: public key A, B-key unchanged
        self.assertEqual(bytes(card.blocks[3][:6]), mad.MADWriter.MAD_KEY)
        self.assertEqual(bytes(card.blocks[3][10:]), FACTORY_KEY)

    def test_blank_1k(self):
        card = ClassicCard()
        api, _ = make_api(card)
        msg = ndef_message(20)
        run(api, api.write_ndef, msg)
        self.check_card(card, msg, owner_sector=2)
        self.assertEqual(card.sector_aid(2), 0x0004)  # owner (holder) info
        self.assertIn(b"Open Development LLC", card.sector_data(2))

    def test_mad_is_written_last(self):
        card = ClassicCard()
        api, _ = make_api(card)
        run(api, api.write_ndef, ndef_message(20))
        self.assertEqual(card.log[-3:], [1, 2, 3])
        self.assertTrue(all(b > 3 for b in card.log[:-3]))

    def test_mini_has_five_sectors(self):
        card = ClassicCard(sectors=5, type_id=2)
        api, _ = make_api(card)
        msg = ndef_message(95)  # 3 sectors + the owner sector fill the whole Mini
        run(api, api.write_ndef, msg)
        self.check_card(card, msg, owner_sector=4)
        self.assertEqual(max(card.log), 19)

    def test_message_that_does_not_fit_touches_nothing(self):
        card = ClassicCard(sectors=5, type_id=2)
        api, _ = make_api(card)
        with self.assertRaisesRegex(RuntimeError, "will not fit") as ctx:
            run(api, api.write_ndef, ndef_message(150))
        self.assertNotIn("format", str(ctx.exception))  # not a "dirty card" problem
        self.assertEqual(card.log, [])

    def test_trailer_write_failure_is_reported(self):
        card = ClassicCard()
        card.fail_blocks = {7}
        api, _ = make_api(card)
        with self.assertRaisesRegex(RuntimeError, "trailer"):
            run(api, api.write_ndef, ndef_message(20))

    def test_rewrite_needs_format_first(self):
        card = ClassicCard()
        api, reader = make_api(card)
        run(api, api.write_ndef, ndef_message(20))

        with self.assertRaisesRegex(RuntimeError, "format"):
            run(api, api.write_ndef, ndef_message(30))

        run(api, api.format_mfc)
        for sector in range(16):
            self.assertEqual(bytes(card.blocks[sector * 4 + 3]), mad.MADWriter.FACTORY_TRAILER)

        msg = ndef_message(30)
        run(api, api.write_ndef, msg)
        self.check_card(card, msg, owner_sector=2)

    def test_format_of_mini_stops_at_sector_4(self):
        card = ClassicCard(sectors=5, type_id=2)
        api, reader = make_api(card)
        run(api, api.write_ndef, ndef_message(95))
        run(api, api.format_mfc)
        self.assertNotIn("AT+R23", reader.commands)
        self.assertIn("AT+R19", reader.commands)

    def test_cloff_is_applied(self):
        card = ClassicCard()
        api, _ = make_api(card)
        api._cl_offset = 3
        msg = ndef_message(20)
        run(api, api.write_ndef, msg)
        self.assertEqual(card.sector_aid(3), 0xE103)
        self.assertEqual(card.sector_aid(1), 0)


class ProtocolTest(unittest.TestCase):
    def test_every_write_uses_the_colon_form(self):
        for card in (UltralightCard(3, 16), ClassicCard()):
            api, reader = make_api(card)
            run(api, api.write_ndef, ndef_message(10))
            writes = [c for c in reader.commands if c.startswith("AT+W")]
            self.assertTrue(writes)
            self.assertTrue(all(":" in c for c in writes), writes)

    def test_not_an_odrfid_reader(self):
        card = UltralightCard(3, 16)
        reader = FakeReader(card, text="Something Else")
        api = apimod.ODRFID_API(reader)
        api.REPLY_TIMEOUT = 0.05
        with self.assertRaisesRegex(RuntimeError, "not an ODRFID"):
            run(api, api.scanoff)

    def test_stale_output_does_not_break_the_first_command(self):
        card = UltralightCard(3, 16)
        reader = FakeReader(card)
        reader.tx += b"\r\nOK\r\n\r\n+UID=DEADBEEF00\r\n\r\nOK\r\n"  # left over from somebody else
        api = apimod.ODRFID_API(reader)
        api.REPLY_TIMEOUT = 0.05
        run(api, api.scanoff)
        self.assertEqual(reader.commands, ["ATH", "ATI", "AT+SCAN0"])

    def test_one_bad_reply_to_ati_is_retried(self):
        class Flaky(FakeReader):
            bad = 1
            def handle(self, cmd):
                if cmd == "ATI" and self.bad:
                    self.bad -= 1
                    return self.ok()  # e.g. a late "OK" instead of the identification
                super().handle(cmd)
        reader = Flaky(UltralightCard(3, 16))
        api = apimod.ODRFID_API(reader)
        api.REPLY_TIMEOUT = 0.05
        with mock.patch.object(time, "sleep"):
            run(api, api.scanoff)

    def test_not_an_odrfid_reader_shows_the_reply(self):
        reader = FakeReader(UltralightCard(3, 16), text="Something Else")
        api = apimod.ODRFID_API(reader)
        api.REPLY_TIMEOUT = 0.05
        with mock.patch.object(time, "sleep"), self.assertRaisesRegex(RuntimeError, r"not an ODRFID reader \(reply: .*Something Else"):
            run(api, api.scanoff)

    def test_busy_port_gives_a_clear_error(self):
        class FakeSerialException(Exception):
            pass

        class Busy:
            def __init__(self, **kwargs):
                self.kwargs = kwargs
                raise FakeSerialException("Could not exclusively lock port")

        fake = mock.Mock(Serial=Busy, SerialException=FakeSerialException)
        with mock.patch.dict(sys.modules, {"serial": fake}), \
                self.assertRaisesRegex(RuntimeError, "Cannot open /dev/ttyX.*close other programs"):
            cli._serial_port(mock.Mock(port="/dev/ttyX", baud=115200))

    def test_port_is_opened_exclusively(self):
        opened = {}

        class Port:
            def __init__(self, **kwargs):
                opened.update(kwargs)

        fake = mock.Mock(Serial=Port, SerialException=Exception)
        with mock.patch.dict(sys.modules, {"serial": fake}):
            cli._serial_port(mock.Mock(port="/dev/ttyX", baud=115200))
        self.assertTrue(opened["exclusive"])

    def test_silent_reader_is_an_error(self):
        class Silent:
            def write(self, data): pass
            def flush(self): pass
            def read(self, size=1): return b""
        api = apimod.ODRFID_API(Silent())
        api.REPLY_TIMEOUT = 0.05
        with self.assertRaisesRegex(RuntimeError, "No reply"):
            api._send("ATH")

    def test_reply_split_in_chunks_is_collected(self):
        class Chunky(FakeReader):
            def read(self, size=1):
                return super().read(1)
            in_waiting = 0
        reader = Chunky(UltralightCard(3, 16))
        api = apimod.ODRFID_API(reader)
        self.assertEqual(api._send(("ATH", "ATI"))[1], reader.text)

    def test_unsupported_tag_type(self):
        api, _ = make_api(UltralightCard(4, 48))  # Ultralight C: the firmware has no read/write for it
        with self.assertRaisesRegex(ValueError, "not supported"):
            run(api, api.write_ndef, ndef_message(10))

    def test_db_line_split(self):
        self.assertEqual(cli.split_db_line("hello world\tID 1\n"), ("hello world", "ID 1"))
        self.assertEqual(cli.split_db_line("data 123"), ("data", "123"))
        self.assertIsNone(cli.split_db_line("single"))

    def test_subcommand_is_required(self):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            cli.main([])


class RawAndMimeTest(unittest.TestCase):
    URI = bytes.fromhex("D1010B550168747470732E2F2F78")  # not a real URI record, framing only

    def test_framing_accepts_valid_messages(self):
        # MB|ME|SR Text record, payload = 02 'en' 'Hello!'
        self.assertEqual(cli.check_ndef_message(bytes.fromhex("D1010954" + "02656E" + "48656C6C6F21")), 1)
        self.assertEqual(cli.check_ndef_message(bytes.fromhex("D00000")), 1)  # one empty record
        # two records: MB|SR (U, 1 byte payload) + ME|SR (T, payload 02 'en' 'x')
        self.assertEqual(cli.check_ndef_message(bytes.fromhex("91010155" + "00" + "5101045402656E78")), 2)
        # long form of the payload length + id: C9 = MB|ME|IL|TNF 1, type 'T', id 'I', payload 'ab'
        self.assertEqual(cli.check_ndef_message(bytes([0xC9, 0x01, 0, 0, 0, 2, 1, 0x54, 0x49, 0x61, 0x62])), 1)

    def test_framing_rejects_broken_messages(self):
        bad = {
            "empty": b"",
            "payload cut short": bytes.fromhex("D1010554"),
            "header cut short": bytes.fromhex("D101"),
            "no ME on the last record": bytes.fromhex("9101015478"),
            "no MB on the first record": bytes.fromhex("5101015478"),
            "trailing bytes": bytes.fromhex("D10101547800"),
            "ME in the middle": bytes.fromhex("D101015478" + "D101015478"),
            "second record is missing": bytes.fromhex("91010154" + "78"),
            "chunked": bytes.fromhex("B1010154" + "78"),
            "empty record with payload": bytes.fromhex("D0000100"),
            "no type": bytes.fromhex("D1000178"),
            "reserved TNF": bytes.fromhex("D7000000"),
        }
        for name, data in bad.items():
            with self.subTest(name), self.assertRaises(ValueError):
                cli.check_ndef_message(data)

    def raw_args(self, **kw):
        base = dict(hex=None, file=None, no_check=False)
        base.update(kw)
        return mock.Mock(**base)

    def test_raw_hex_and_file(self):
        msg = ndef_message(10)
        self.assertEqual(cli.prepare_raw(self.raw_args(hex=msg.hex())), msg)
        self.assertEqual(cli.prepare_raw(self.raw_args(hex=" ".join("{:02x}".format(b) for b in msg))), msg)  # spaces are fine
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "message.ndef"
            path.write_bytes(msg)
            self.assertEqual(cli.prepare_raw(self.raw_args(file=str(path))), msg)

    def test_raw_rejects_bad_input(self):
        with self.assertRaisesRegex(ValueError, "hex"):
            cli.prepare_raw(self.raw_args(hex="D10"))
        with self.assertRaisesRegex(ValueError, "extra"):
            cli.prepare_raw(self.raw_args(hex="D10101547800"))
        # --no-check passes it through
        self.assertEqual(cli.prepare_raw(self.raw_args(hex="D10101547800", no_check=True)), bytes.fromhex("D10101547800"))

    def mime_args(self, rtype, **kw):
        base = dict(type=rtype, file=None, hex=None, text=None, id=None)
        base.update(kw)
        return mock.Mock(**base)

    def test_mime_type_validation(self):
        for bad in ("plain", "a/b/c", "", "text/", "/vcard", "urn:nfc:ext:nodomain", "text/v card"):
            with self.subTest(bad), self.assertRaisesRegex(ValueError, "neither"):
                cli.prepare_mime(self.mime_args(bad, text="x"))
        if ndef is None:
            self.skipTest("ndeflib is not installed")
        for good in ("text/vcard", "application/vnd.wfa.wsc", "image/svg+xml", "urn:nfc:ext:open-dev.ru:odrfid"):
            with self.subTest(good):
                cli.prepare_mime(self.mime_args(good, text="x"))

    @unittest.skipIf(ndef is None, "ndeflib is not installed")
    def test_mime_record_roundtrip_to_tag(self):
        vcard = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Иван\r\nEND:VCARD\r\n"
        data = cli.prepare_mime(self.mime_args("Text/VCard", text=vcard, id="c1"))
        cli.check_ndef_message(data)
        record = list(ndef.message_decoder(data, known_types={}))[0]
        self.assertEqual((record.type, record.name, bytes(record.data)), ("text/vcard", "c1", vcard.encode("utf-8")))

        card = UltralightCard(25, 231, cc=bytes.fromhex("E1106D00"))
        api, _ = make_api(card)
        run(api, api.write_ndef, data)
        self.assertEqual(card.ndef_state(), data)

    @unittest.skipIf(ndef is None, "ndeflib is not installed")
    def test_mime_payload_sources(self):
        by_hex = cli.prepare_mime(self.mime_args("application/octet-stream", hex="00 01 FF"))
        record = list(ndef.message_decoder(by_hex, known_types={}))[0]
        self.assertEqual(bytes(record.data), b"\x00\x01\xff")
        with self.assertRaisesRegex(ValueError, "hex"):
            cli.prepare_mime(self.mime_args("application/octet-stream", hex="0"))
        with self.assertRaisesRegex(ValueError, "too long"):
            cli.prepare_mime(self.mime_args("text/plain", text="x", id="i" * 256))

    @unittest.skipIf(ndef is None, "ndeflib is not installed")
    def test_raw_with_two_records_goes_to_the_tag(self):
        msg = b"".join(ndef.message_encoder([ndef.UriRecord("https://open-dev.ru"), ndef.TextRecord("hi", "en")]))
        self.assertEqual(cli.check_ndef_message(msg), 2)
        card = ClassicCard()
        api, _ = make_api(card)
        run(api, api.write_ndef, cli.prepare_raw(self.raw_args(hex=msg.hex())))
        self.assertEqual(card.sector_aid(1), 0xE103)

    def test_cli_parses_the_new_commands(self):
        for argv, func in ((["raw", "--hex", "D00000"], cli.prepare_raw),
                           (["mime", "text/plain", "--text", "x"], cli.prepare_mime)):
            with self.subTest(argv[0]), mock.patch.object(cli, func.__name__, return_value=b"") as prep, \
                    mock.patch.object(apimod.ODRFID_API, "write_ndef") as write, mock.patch.object(cli, "_serial_port"):
                cli.main(argv)
                self.assertTrue(prep.called or write.called)
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            cli.main(["raw"])  # needs --hex or --file
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            cli.main(["mime", "text/plain"])  # needs a payload


@unittest.skipIf(ndef is None, "ndeflib is not installed")
class EncodersTest(unittest.TestCase):
    def test_text_and_uri_roundtrip_to_tag(self):
        args = mock.Mock(message="Привет", lang="ru", utf16=False)
        data = cli.prepare_text(args)
        card = UltralightCard(23, 45, cc=bytes.fromhex("E1101200"))
        api, _ = make_api(card)
        run(api, api.write_ndef, data)
        records = list(ndef.message_decoder(card.ndef_state()))
        self.assertEqual(records[0].text, "Привет")

        data = cli.prepare_url(mock.Mock(link="https://open-dev.ru"))
        self.assertEqual(list(ndef.message_decoder(data))[0].iri, "https://open-dev.ru")

    def test_smartposter(self):
        args = mock.Mock(uri="https://open-dev.ru", title=[["en", "Open Dev"], ["ru", "Опен Дев"]], action="exec",
                         icon=None, size=-1, mime=None)
        record = list(ndef.message_decoder(cli.prepare_sp(args)))[0]
        self.assertEqual(record.resource.iri, "https://open-dev.ru")
        self.assertEqual(record.titles["ru"], "Опен Дев")


# The behaviour that makes the executable usable by people who do not read tracebacks

class MainTest(unittest.TestCase):
    def run_main(self, argv, **patches):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch("sys.stdout", out), mock.patch("sys.stderr", err):
            code = cli.main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_version(self):
        out = io.StringIO()
        with mock.patch("sys.stdout", out), self.assertRaises(SystemExit) as ctx:
            cli.main(["--version"])
        self.assertEqual(ctx.exception.code, 0)
        self.assertEqual(out.getvalue().strip(), "ndef-writer " + ndef_writer.__version__)

    def test_licenses_are_shipped(self):
        out = io.StringIO()
        with mock.patch("sys.stdout", out), self.assertRaises(SystemExit) as ctx:
            cli.main(["--licenses"])
        self.assertEqual(ctx.exception.code, 0)
        for word in ("ndeflib", "pyserial", "Python", "ISC", "BSD-3-Clause"):
            self.assertIn(word, out.getvalue())

    def test_failure_is_one_line_and_exit_code_1(self):
        code, out, err = self.run_main(["raw", "--hex", "D10101547800"])
        self.assertEqual(code, 1)
        self.assertEqual(err.strip(), "Error: 1 extra byte(s) after the last record")
        self.assertNotIn("Traceback", err)

    def test_verbose_adds_the_traceback(self):
        code, out, err = self.run_main(["-v", "raw", "--hex", "D10101547800"])
        self.assertEqual(code, 1)
        self.assertIn("Traceback", err)
        self.assertIn("Error: 1 extra byte(s)", err)

    def test_missing_file_is_an_error_not_a_crash(self):
        code, out, err = self.run_main(["raw", "--file", "/no/such/file.ndef"])
        self.assertEqual(code, 1)
        self.assertTrue(err.startswith("Error: "))
        self.assertIn("No such file", err)

    def test_busy_or_missing_port_is_reported(self):
        with mock.patch.object(cli, "_serial_port", side_effect=RuntimeError("Cannot open X: busy")):
            code, out, err = self.run_main(["raw", "--hex", "D00000"])
        self.assertEqual((code, err.strip()), (1, "Error: Cannot open X: busy"))

    def test_a_bug_does_not_dump_a_traceback_by_default(self):
        with mock.patch.object(cli, "run", side_effect=KeyError("boom")):
            code, out, err = self.run_main(["raw", "--hex", "D00000"])
        self.assertEqual(code, 2)
        self.assertIn("Internal error (KeyError", err)
        self.assertNotIn("Traceback", err)

    def test_ctrl_c_and_closed_stdin(self):
        with mock.patch.object(cli, "run", side_effect=KeyboardInterrupt):
            self.assertEqual(self.run_main(["raw", "--hex", "D00000"])[0], 130)
        with mock.patch.object(cli, "run", side_effect=EOFError):
            code, out, err = self.run_main(["raw", "--hex", "D00000"])
        self.assertEqual(code, 1)
        self.assertIn("Error:", err)

    def test_success_returns_zero(self):
        with mock.patch.object(cli, "_serial_port"), mock.patch.object(apimod.ODRFID_API, "write_ndef") as write:
            code, out, err = self.run_main(["raw", "--hex", "D00000"])
        self.assertEqual(code, 0)
        write.assert_called_once_with(bytes.fromhex("D00000"))

    def test_console_that_cannot_show_a_character_does_not_abort(self):
        class Narrow(io.TextIOWrapper):
            pass
        stream = Narrow(io.BytesIO(), encoding="ascii", errors="strict")
        with mock.patch("sys.stdout", stream), mock.patch("sys.stderr", stream):
            cli._console_never_fails()
            print("Привет")  # would raise UnicodeEncodeError without the reconfiguration
            stream.flush()
        self.assertEqual(stream.buffer.getvalue().strip(), b"??????")

    @unittest.skipIf(ndef is None, "ndeflib is not installed")
    def test_batch_file_is_utf8_by_default_and_accepts_a_bom(self):
        card = UltralightCard(23, 45, cc=bytes.fromhex("E1101200"))
        reader = FakeReader(card)
        written = []
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "tags.txt"
            path.write_bytes(b"\xef\xbb\xbf" + "Привет мир\tID-001\n".encode("utf-8"))
            with mock.patch.object(cli, "_serial_port", return_value=mock.MagicMock(__enter__=lambda s: reader, __exit__=lambda *a: False)), \
                    mock.patch("builtins.input", return_value="y"), mock.patch("builtins.print"), \
                    mock.patch.object(apimod.ODRFID_API, "REPLY_TIMEOUT", 0.05):
                code = cli.main(["batch", str(path), "-l", "ru"])
        self.assertEqual(code, 0)
        state = card.ndef_state()
        self.assertIn("Привет мир".encode("utf-8"), state)


class PackagingTest(unittest.TestCase):
    def test_every_command_has_help(self):
        parser = cli.build_parser()
        for command in ("text", "uri", "poster", "raw", "mime", "format", "batch"):
            with self.subTest(command), mock.patch("sys.stdout", io.StringIO()), self.assertRaises(SystemExit) as ctx:
                parser.parse_args([command, "--help"])
            self.assertEqual(ctx.exception.code, 0)

    def test_version_is_semver(self):
        import re
        self.assertRegex(ndef_writer.__version__, r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main()
