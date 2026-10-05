"""Command line interface of ndef-writer."""
import argparse
import sys
import traceback
import re
import struct

from . import __version__
from .api import ODRFID_API


def _ndef():
    try:
        import ndef
    except ImportError:
        raise RuntimeError('Install the ndef lib: `pip install -U ndeflib`')
    return ndef


def _serial_port(args):
    try:
        from serial import Serial, SerialException
    except ImportError:
        raise RuntimeError('Install the serial lib: `pip install pyserial`')
    try:
        # exclusive: a second ndef-writer (or another pyserial program) gets an error instead of garbled replies
        return Serial(port=args.port, baudrate=args.baud, timeout=0.3, exclusive=True)
    except SerialException as err:
        raise RuntimeError('Cannot open {}: {}. Check the port name and close other programs that use the port '
                           '(ODRFIDKit Web, a terminal, another ndef-writer)'.format(args.port, err)) from err


def prepare_text(args):
    ndef = _ndef()
    return b''.join(ndef.message_encoder([ndef.TextRecord(args.message, args.lang or 'en', encoding=('UTF-16' if args.utf16 else 'UTF-8'))]))


def prepare_url(args):
    ndef = _ndef()
    return b''.join(ndef.message_encoder([ndef.UriRecord(args.link)]))


def prepare_sp(args):
    ndef = _ndef()
    icn_data = None
    if args.icon:
        if not args.icon.lower().endswith('.png'):
            raise RuntimeError('Only png files are accepted as icon resources')
        with open(args.icon, 'rb') as fd:
            icn_data = fd.read()
    # ndeflib wants {language: title}, argparse gives [[language, title], ...]
    titles = {lang: text for lang, text in args.title} if args.title else None
    return b''.join(ndef.message_encoder([ndef.SmartposterRecord(args.uri, title=titles,
                                                                 action=(args.action or None), icon=icn_data,
                                                                 resource_size=(None if args.size < 0 else args.size),
                                                                 resource_type=(args.mime or None))]))


def check_ndef_message(data):
    """
    Checks the framing of a ready NDEF message (NFC Forum NDEF 1.0): MB on the first and ME on the last record,
    consistent lengths, no trailing bytes. The payloads are not interpreted. Raises ValueError.
    """
    pos, index = 0, 0
    if not data:
        raise ValueError('NDEF message is empty (an empty record is D00000)')
    while True:
        if pos >= len(data):
            raise ValueError('record {}: unexpected end of the message'.format(index))
        flags = data[pos]
        mb, me, cf, sr, il, tnf = bool(flags & 0x80), bool(flags & 0x40), bool(flags & 0x20), bool(flags & 0x10), bool(flags & 0x08), flags & 0x07
        if mb != (index == 0):
            raise ValueError('record {}: MB flag {}'.format(index, 'missing on the first record' if index == 0 else 'set on a non-first record'))
        if cf:
            raise ValueError('record {}: chunked records are not supported'.format(index))
        if tnf in (0x06, 0x07):
            raise ValueError('record {}: reserved / unchanged TNF {}'.format(index, tnf))
        pos += 1
        header = 1 + (1 if sr else 4) + (1 if il else 0)
        if pos + header > len(data):
            raise ValueError('record {}: truncated header'.format(index))
        type_len = data[pos]
        pos += 1
        if sr:
            payload_len = data[pos]
            pos += 1
        else:
            payload_len = struct.unpack_from('>I', data, pos)[0]
            pos += 4
        id_len = 0
        if il:
            id_len = data[pos]
            pos += 1
        if tnf == 0x00 and (type_len or id_len or payload_len):
            raise ValueError('record {}: an empty record must not carry a type, an id or a payload'.format(index))
        if tnf in (0x01, 0x02, 0x03, 0x04) and type_len == 0:
            raise ValueError('record {}: the type is missing'.format(index))
        pos += type_len + id_len + payload_len
        if pos > len(data):
            raise ValueError('record {}: the record is longer than the message'.format(index))
        index += 1
        if me:
            break
    if pos != len(data):
        raise ValueError('{} extra byte(s) after the last record'.format(len(data) - pos))
    return index


def prepare_raw(args):
    if args.file:
        with open(args.file, 'rb') as fd:
            data = fd.read()
    else:
        try:
            data = bytes.fromhex(args.hex)
        except ValueError as err:
            raise ValueError('Invalid hex string: {}'.format(err)) from err
    if not args.no_check:
        check_ndef_message(data)
    return data


MIME_TYPE_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9!#$&^_.+-]{0,126}/[A-Za-z0-9][A-Za-z0-9!#$&^_.+-]{0,126}$')
EXT_TYPE_RE = re.compile(r'^urn:nfc:ext:[^:/\s]+:[^:\s]+$')


def prepare_mime(args):
    if MIME_TYPE_RE.match(args.type):
        rtype = args.type.lower()  # media types are case-insensitive, NFC Forum recommends lower case
    elif EXT_TYPE_RE.match(args.type):
        rtype = args.type
    else:
        raise ValueError('"{}" is neither a MIME type (type/subtype) nor an external type (urn:nfc:ext:domain:type)'.format(args.type))

    if args.file:
        with open(args.file, 'rb') as fd:
            payload = fd.read()
    elif args.hex is not None:
        try:
            payload = bytes.fromhex(args.hex)
        except ValueError as err:
            raise ValueError('Invalid hex string: {}'.format(err)) from err
    else:
        payload = args.text.encode('utf-8')

    if args.id is not None and len(args.id.encode('utf-8')) > 255:
        raise ValueError('The record id is too long (255 bytes max)')
    ndef = _ndef()
    return b''.join(ndef.message_encoder([ndef.Record(rtype, args.id, payload)]))


def mfc_format(args):
    with _serial_port(args) as tty:
        rfid = ODRFID_API(tty, debug=args.verbose)
        rfid.format_mfc(args.key)


def split_db_line(line):
    """[data]<TAB>[ID]; falls back to the first whitespace when there is no TAB (then data cannot have spaces)"""
    line = line.rstrip('\r\n')
    parts = line.split('\t', 1) if '\t' in line else line.split(maxsplit=1)
    if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
        return None
    return parts[0].strip(), parts[1].strip()


def parse_db(args):
    ANSWER_YES = ('y', 'yes', 'Y', 'Yes')
    ANSWER_NO = ('n', 'no', 'N', 'No')
    ndef = _ndef()

    def _ask(query):
        while 1:
            acc = input(query + ':')
            if acc in ANSWER_YES:
                return True
            if acc in ANSWER_NO:
                return False
            print('Yes or No please')

    def get_data(raw):
        if args.type == 'text':
            return b''.join(ndef.message_encoder([ndef.TextRecord(raw, args.lang or 'en', encoding=('UTF-16' if args.utf16 else 'UTF-8'))]))
        elif args.type == 'uri':
            return b''.join(ndef.message_encoder([ndef.UriRecord(raw)]))
        else:
            raise ValueError('Unsupported type {}'.format(args.type))

    with open(args.file, 'r', encoding=args.enc) as fd, _serial_port(args) as tty:
        rfid = ODRFID_API(tty, debug=args.verbose)
        rfid._cl_offset = args.cloff
        rfid.scanoff()
        for line in fd:
            if not line.strip():
                continue

            parts = split_db_line(line)
            if not parts:
                if _ask('Invalid line {}. Skip? [y/n]'.format(line.strip())):
                    continue
                else:
                    raise RuntimeError('Invalid line in file')
            data, uid = parts
            if _ask('Prepare tag "{}" (will write "{}"). Proceed? [y/n]'.format(uid, data)):
                ndef_data = get_data(data)
                rfid.write_ndef(ndef_data, nocheck=True)
                print('Done! Tag for "{}" processed. Remove the tag'.format(uid))
            else:
                if _ask('Abort processing? [y/n]'):
                    raise RuntimeError('Aborted by user')
                else:
                    print('Skipping {} as requested'.format(uid))
    print('End of file')


def licenses_text():
    """Third-party license texts that are shipped inside the package (and so inside the executable)"""
    try:
        from importlib import resources
        return resources.files(__package__).joinpath('THIRD_PARTY_LICENSES.txt').read_text(encoding='utf-8')
    except (OSError, ImportError, AttributeError):
        return 'The third-party license texts are not available in this installation.\n'


class _LicensesAction(argparse.Action):
    def __init__(self, option_strings, dest, **kwargs):
        super().__init__(option_strings, dest, nargs=0, default=argparse.SUPPRESS, help='Show the licenses of the bundled third-party components and exit')

    def __call__(self, parser, namespace, values, option_string=None):
        sys.stdout.write(licenses_text())
        parser.exit()


def build_parser():
    def lang_pair(arg):
        return arg.split(':', 1) if ':' in arg else ['en', arg]

    parser = argparse.ArgumentParser(
        prog='ndef-writer',
        description='Writes NDEF messages to Mifare Classic, Ultralight and NTAG tags using an ODRFID reader. '
                    'The tag is placed on the reader after the command is started.')
    parser.add_argument('-p', '--port', type=str, default='/dev/ttyACM0', help='ODRFID reader port (ex: /dev/ttyACM0, COM3)')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='Baudrate')
    parser.add_argument('--cloff', type=int, default=None, help='Offset (in sectors, >=1) in the MFC tag')
    parser.add_argument('-v', '--verbose', action='store_true', default=False, help='Show the AT exchange and the NDEF bytes, print tracebacks')
    parser.add_argument('--version', action='version', version='%(prog)s ' + __version__)
    parser.add_argument('--licenses', action=_LicensesAction)
    subparsers = parser.add_subparsers(help='Actions', dest='command', required=True, metavar='{text,uri,poster,raw,mime,format,batch}')

    parser_text = subparsers.add_parser('text', help='NDEF Text Record')
    parser_text.add_argument('message', help='Text message')
    parser_text.add_argument('--utf16', action='store_true', default=False, help='Encode as UTF-16')
    parser_text.add_argument('-l', '--lang', default='en', help='Language')
    parser_text.set_defaults(func=prepare_text)

    parser_url = subparsers.add_parser('uri', help='NDEF URI Record')
    parser_url.add_argument('link', help='Internationalized resource identifier (ex: web url)')
    parser_url.set_defaults(func=prepare_url)

    parser_sp = subparsers.add_parser('poster', help='NDEF Smartposter Record')
    parser_sp.add_argument('uri', help='Internationalized resource identifier (ex: web url)')
    parser_sp.add_argument('--title', type=lang_pair, nargs='+', help='Poster title (format: "language:title" or "title")')
    parser_sp.add_argument('--action', type=str, choices=['exec', 'save', 'edit'], default='', help='Poster action')
    parser_sp.add_argument('--icon', type=str, help='Icon file (png, optional)', default=None)
    parser_sp.add_argument('--mime', type=str, default=None, help='Resource type (in the form of mime-type)')
    parser_sp.add_argument('--size', type=int, default=-1, help='Resource size (optional)')
    parser_sp.set_defaults(func=prepare_sp)

    parser_raw = subparsers.add_parser('raw', help='Ready NDEF message (any records, also several) from a hex string or a file')
    raw_src = parser_raw.add_mutually_exclusive_group(required=True)
    raw_src.add_argument('--hex', help='NDEF message as a hex string (ex: D1010C5402656E48656C6C6F)')
    raw_src.add_argument('--file', help='File with the binary NDEF message')
    parser_raw.add_argument('--no-check', action='store_true', default=False, help='Do not check the NDEF framing')
    parser_raw.set_defaults(func=prepare_raw)

    parser_mime = subparsers.add_parser('mime', help='NDEF MIME record (ex: text/vcard) or external type record (urn:nfc:ext:domain:type)')
    parser_mime.add_argument('type', help='MIME type (type/subtype) or external type (urn:nfc:ext:domain:type)')
    mime_src = parser_mime.add_mutually_exclusive_group(required=True)
    mime_src.add_argument('--file', help='Payload file (ex: contact.vcf)')
    mime_src.add_argument('--hex', help='Payload as a hex string')
    mime_src.add_argument('--text', help='Payload as a text (UTF-8)')
    parser_mime.add_argument('--id', default=None, help='Record id (optional)')
    parser_mime.set_defaults(func=prepare_mime)

    parser_mfcl = subparsers.add_parser('format', help='Reset the keys / access bits of a Mifare Classic NDEF-formatted card to factory defaults (data blocks are not erased)')
    parser_mfcl.add_argument('--key', type=str, default='FFFFFFFFFFFF', help='B-key')
    parser_mfcl.set_defaults(func=mfc_format)

    parser_batch = subparsers.add_parser('batch', help='Batch mode (reads [data]<TAB>[ID] from file)')
    parser_batch.add_argument('file', help='Input file (DB)')
    parser_batch.add_argument('--enc', help='File encoding (default: utf-8, a BOM is accepted)', default='utf-8-sig')
    parser_batch.add_argument('--type', choices=['text', 'uri'], default='text', help='Data type (default: text)')
    parser_batch.add_argument('-l', '--lang', default='en', help='Language (for text data)')
    parser_batch.add_argument('--utf16', action='store_true', default=False, help='Encode as UTF-16 (for text data)')
    parser_batch.set_defaults(func=parse_db)
    return parser


def run(args):
    ndef_data = args.func(args)
    if ndef_data is None:
        return  # the command did everything itself (format, batch)

    if args.verbose:
        print('NDEF message: ', ndef_data)
    with _serial_port(args) as tty:
        rfid = ODRFID_API(tty, debug=args.verbose)
        if args.cloff is not None:
            rfid._cl_offset = args.cloff
        rfid.write_ndef(ndef_data)


def _console_never_fails():
    """A console code page that cannot show a character must not abort the program"""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, 'reconfigure', None)
        if reconfigure:
            try:
                reconfigure(errors='replace')
            except (ValueError, OSError):
                pass


def main(argv=None):
    """Returns the exit code: 0 - done, 1 - the operation failed, 2 - an internal error, 130 - interrupted"""
    _console_never_fails()
    args = build_parser().parse_args(argv)
    try:
        run(args)
    except KeyboardInterrupt:
        print('\nInterrupted', file=sys.stderr)
        return 130
    except (RuntimeError, ValueError, OSError, EOFError) as err:
        if args.verbose:
            traceback.print_exc()
        print('Error: {}'.format(err or type(err).__name__), file=sys.stderr)
        return 1
    except Exception as err:  # a bug: keep the output readable, the details are one flag away
        if args.verbose:
            traceback.print_exc()
        print('Internal error ({}: {}). Run with -v for the details.'.format(type(err).__name__, err), file=sys.stderr)
        return 2
    return 0
