# Exif as a camera writes it, for the test images of JPEG Info and the
# Workbench (tests/jpeg-info/make-fixtures.py, tests/workbench-check.py):
# the make, the model, the software, maker notes and a JPEG thumbnail, in
# a little endian TIFF structure. Plain Python, no GIMP.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import struct


def tiff_ifd(entries, start, next_ifd):
    """a little endian IFD at offset start: entries (tag, type, count,
    bytes); returns its bytes, values that do not fit after it"""
    n = len(entries)
    data_at = start + 2 + 12 * n + 4
    head = struct.pack('<H', n)
    tail = b''
    for tag, typ, count, value in sorted(entries):
        if len(value) <= 4:
            head += struct.pack('<HHI', tag, typ, count) + value.ljust(4, b'\x00')
        else:
            head += struct.pack('<HHII', tag, typ, count, data_at + len(tail))
            tail += value + (b'\x00' if len(value) % 2 else b'')
    return head + struct.pack('<I', next_ifd) + tail


def exif_segment(make, model, software, thumb):
    """APP1 Exif: IFD0, an Exif IFD with maker notes, IFD1 with the thumbnail"""
    def ascii(s):
        b = s.encode() + b'\x00'
        return (2, len(b), b)
    note = b'Nikon\x00' + bytes(range(64))
    # sizes first, offsets after (a fixed layout)
    ifd0_at = 8
    ifd0 = [(0x010F,) + ascii(make), (0x0110,) + ascii(model), (0x0131,) + ascii(software),
            (0x8769, 4, 1, b'\x00' * 4)]
    size0 = len(tiff_ifd(ifd0, ifd0_at, 0))
    exif_at = ifd0_at + size0
    exif_ifd = [(0x927C, 7, len(note), note)]
    size_e = len(tiff_ifd(exif_ifd, exif_at, 0))
    ifd1_at = exif_at + size_e
    ifd1 = [(0x0103, 3, 1, struct.pack('<H', 6)), (0x0201, 4, 1, b'\x00' * 4),
            (0x0202, 4, 1, struct.pack('<I', len(thumb)))]
    size1 = len(tiff_ifd(ifd1, ifd1_at, 0))
    thumb_at = ifd1_at + size1
    ifd0[3] = (0x8769, 4, 1, struct.pack('<I', exif_at))
    ifd1[1] = (0x0201, 4, 1, struct.pack('<I', thumb_at))
    tiff = (b'II*\x00' + struct.pack('<I', ifd0_at) + tiff_ifd(ifd0, ifd0_at, ifd1_at) +
            tiff_ifd(exif_ifd, exif_at, 0) + tiff_ifd(ifd1, ifd1_at, 0) + thumb)
    body = b'Exif\x00\x00' + tiff
    return b'\xff\xe1' + struct.pack('>H', len(body) + 2) + body


def with_exif(path, exif):
    """puts the Exif segment right after SOI (the file has none of its own)"""
    d = open(path, 'rb').read()
    open(path, 'wb').write(d[:2] + exif + d[2:])


