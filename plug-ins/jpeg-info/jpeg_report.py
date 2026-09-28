# JPEG Info: what a JPEG file says about how it was saved.
#
# Reads the file on disk (never the pixels GIMP decoded), in pure Python
# 3 (the Python of GIMP, without numpy):
#
# - its structure: the markers, the frame (baseline, progressive, the
#   chroma subsampling), the Huffman tables (the standard ones or
#   optimised), restart markers, data after the end of the image;
# - its metadata: JFIF, Exif (camera, software, dates, maker notes, the
#   embedded thumbnail), XMP, ICC, Photoshop's resources (the quality of
#   Photoshop's Save As), Adobe, MPF, C2PA, comments;
# - the quantisation tables, and from them the quality of the last save:
#   exactly the IJG (libjpeg) tables at some quality, or one of mozjpeg's
#   other base tables, or else the nearest IJG quality (Sherloq's
#   estimate);
# - the tables' compression signature, looked up in JPEGsnoop's database
#   of cameras and programs, and JPEGsnoop's assessment of whether the
#   file was processed (its classes 1 to 4);
# - hints of an earlier JPEG compression on the same 8 x 8 grid (double
#   compression): the histograms of the file's own DCT coefficients,
#   decoded here from the entropy coded data, against a model of double
#   quantisation (maximum likelihood); and the steps of the first save,
#   with the quality they fit;
# - the Exif thumbnail: its size and framing against the image's, its own
#   tables. The pixel comparison is compare_pixels () below, which the
#   plug-in feeds with the decoded thumbnail and the image scaled down.
#
# Every result is an indicator, not proof: see HONEST below.
#
# Methods and sources:
#
# - JPEG itself: ITU-T T.81 (1992), Annex B (syntax), Annex F and G
#   (sequential and progressive Huffman decoding), Annex K (the example
#   tables, which libjpeg scales for its quality setting).
# - The IJG quality scale: libjpeg's jpeg_quality_scaling () and
#   jpeg_add_quant_table () (jcparam.c), reproduced exactly (integer
#   arithmetic, baseline clamping to 255). mozjpeg's alternative base
#   tables (jcparam.c of github.com/mozilla/mozjpeg, IJG and BSD
#   licences): the numbers are copied, with their sources.
# - Sherloq (Guido Bartoli, github.com/GuidoBartoli/sherloq, GPL-3.0),
#   gui/sherloq_app/tools/jpeg/quality.py: "last saved quality" as the IJG
#   quality whose tables are nearest (mean absolute difference, chroma
#   weighted twice) minus that distance, and the "level" of a table. Its
#   method is followed (sherloq_estimate ()); the code here is new.
# - JPEGsnoop (Calvin Hass, github.com/ImpulseAdventure/JPEGsnoop,
#   GPL-2.0-or-later): the compression signature (JfifDecode.cpp,
#   PrepareSignatureSingle), its database (jpegsnoop-signatures.tsv, made
#   from Signatures.inl by update-signatures.py), the lists of IJG based
#   programs, of comment strings of editors and of make/model exceptions
#   (DbSigs.cpp, Signatures.inl), the assessment classes
#   (CompareSignature) and the Photoshop quality resource 0x0406
#   (DecodePs.cpp).
# - Double compression: J. Lukas and J. Fridrich, "Estimation of Primary
#   Quantization Matrix in Double Compressed JPEG Images", DFRWS 2003 (the
#   double quantisation effect, its periodic histograms, the primary steps
#   by fitting); A. C. Popescu and H. Farid, "Statistical Tools for
#   Digital Forensics", IH 2004 (the periodic artifacts of double
#   quantisation); Z. Fan and R. L. de Queiroz, "Identification of Bitmap
#   Compression History: JPEG Detection and Quantizer Estimation", IEEE
#   TIP 12 (2), 2003 (maximum likelihood estimation of quantiser steps).
#   The model here (a Laplacian times a power law for the coefficients,
#   rounding noise between the two saves) is this module's own.
# - Thumbnails: E. Kee and H. Farid, "Digital Image Authentication from
#   Thumbnails", SPIE 2010 (a camera makes its thumbnail by a crop, a
#   resize and a JPEG save of its own; an edited image keeps the old
#   thumbnail unless the editor writes a new one); Sherloq's
#   tools/metadata/thumbnail.py compares the thumbnail, scaled up, with
#   the image.
#
# Copyright 2026 David
# Parts follow Sherloq (Copyright Guido Bartoli and contributors,
# GPL-3.0) and JPEGsnoop (Copyright 2017 Calvin Hass, GPL-2.0-or-later),
# as said above.
# SPDX-License-Identifier: GPL-3.0-or-later

import array
import hashlib
import math
import os
import re
import struct
import time

VERSION = '1.0'

# ---------------------------------------------------------------- tables

# ZIGZAG[k]: the natural (row by row) index of the k-th coefficient in
# zigzag order (T.81 figure A.6)
ZIGZAG = (
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63)

# the base tables of mozjpeg's jcparam.c (std_luminance_quant_tbl and
# std_chrominance_quant_tbl), natural order; the first is T.81 Annex K,
# the one libjpeg and nearly every program scales. None: the chroma table
# is the luma one.
BASE_TABLES = (
    ('JPEG Annex K (libjpeg, IJG)', (
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68, 109, 103, 77,
        24, 35, 55, 64, 81, 104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103, 99), (
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99)),
    ('flat (mozjpeg table 1)', (16,) * 64, None),
    ('MSSIM-tuned on Kodak images (mozjpeg table 2)', (
        12, 17, 20, 21, 30, 34, 56, 63,
        18, 20, 20, 26, 28, 51, 61, 55,
        19, 20, 21, 26, 33, 58, 69, 55,
        26, 26, 26, 30, 46, 87, 86, 66,
        31, 33, 36, 40, 46, 96, 100, 73,
        40, 35, 46, 62, 81, 100, 111, 91,
        46, 66, 76, 86, 102, 121, 120, 101,
        68, 90, 90, 96, 113, 102, 105, 103), (
        8, 12, 15, 15, 86, 96, 96, 98,
        13, 13, 15, 26, 90, 96, 99, 98,
        12, 15, 18, 96, 99, 99, 99, 99,
        17, 16, 90, 96, 99, 99, 99, 99,
        96, 96, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99)),
    ('ImageMagick table (mozjpeg table 3, the default of its cjpeg)', (
        16, 16, 16, 18, 25, 37, 56, 85,
        16, 17, 20, 27, 34, 40, 53, 75,
        16, 20, 24, 31, 43, 62, 91, 135,
        18, 27, 31, 40, 53, 74, 106, 156,
        25, 34, 43, 53, 69, 94, 131, 189,
        37, 40, 62, 74, 94, 124, 169, 238,
        56, 53, 91, 106, 131, 169, 226, 311,
        85, 75, 135, 156, 189, 238, 311, 418), None),
    ('PSNR-HVS-M tuned (mozjpeg table 4)', (
        9, 10, 12, 14, 27, 32, 51, 62,
        11, 12, 14, 19, 27, 44, 59, 73,
        12, 14, 18, 25, 42, 59, 79, 78,
        17, 18, 25, 42, 61, 92, 87, 92,
        23, 28, 42, 75, 79, 112, 112, 99,
        40, 42, 59, 84, 88, 124, 132, 111,
        42, 64, 78, 95, 105, 126, 125, 99,
        70, 75, 100, 102, 116, 100, 107, 98), (
        9, 10, 17, 19, 62, 89, 91, 97,
        12, 13, 18, 29, 84, 91, 88, 98,
        14, 19, 29, 93, 95, 95, 98, 97,
        20, 26, 84, 88, 95, 95, 98, 94,
        26, 86, 91, 93, 97, 99, 98, 99,
        99, 100, 98, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        97, 97, 99, 99, 99, 99, 97, 99)),
    ('Klein, Silverstein and Carney 1992 (mozjpeg table 5)', (
        10, 12, 14, 19, 26, 38, 57, 86,
        12, 18, 21, 28, 35, 41, 54, 76,
        14, 21, 25, 32, 44, 63, 92, 136,
        19, 28, 32, 41, 54, 75, 107, 157,
        26, 35, 44, 54, 70, 95, 132, 190,
        38, 41, 63, 75, 95, 125, 170, 239,
        57, 54, 92, 107, 132, 170, 227, 312,
        86, 76, 136, 157, 190, 239, 312, 419), None),
    ('Watson, Taylor and Borthwick 1997 (mozjpeg table 6)', (
        7, 8, 10, 14, 23, 44, 95, 241,
        8, 8, 11, 15, 25, 47, 102, 255,
        10, 11, 13, 19, 31, 58, 127, 255,
        14, 15, 19, 27, 44, 83, 181, 255,
        23, 25, 31, 44, 72, 136, 255, 255,
        44, 47, 58, 83, 136, 255, 255, 255,
        95, 102, 127, 181, 255, 255, 255, 255,
        241, 255, 255, 255, 255, 255, 255, 255), None),
    ('Ahumada, Watson and Peterson 1993 (mozjpeg table 7)', (
        15, 11, 11, 12, 15, 19, 25, 32,
        11, 13, 10, 10, 12, 15, 19, 24,
        11, 10, 14, 14, 16, 18, 22, 27,
        12, 10, 14, 18, 21, 24, 28, 33,
        15, 12, 16, 21, 26, 31, 36, 42,
        19, 15, 18, 24, 31, 38, 45, 53,
        25, 19, 22, 28, 36, 45, 55, 65,
        32, 24, 27, 33, 42, 53, 65, 77), None),
    ('Peterson, Ahumada and Watson 1993 (mozjpeg table 8)', (
        14, 10, 11, 14, 19, 25, 34, 45,
        10, 11, 11, 12, 15, 20, 26, 33,
        11, 11, 15, 18, 21, 25, 31, 38,
        14, 12, 18, 24, 28, 33, 39, 47,
        19, 15, 21, 28, 36, 43, 51, 59,
        25, 20, 25, 33, 43, 54, 64, 74,
        34, 26, 31, 39, 51, 64, 77, 91,
        45, 33, 38, 47, 59, 74, 91, 108), None),
)
ANNEX_K_LUMA = BASE_TABLES[0][1]
ANNEX_K_CHROMA = BASE_TABLES[0][2]

# the Huffman tables of T.81 Annex K.3 (counts per code length 1 to 16,
# symbols), which libjpeg writes unless told to optimise
STD_HUFFMAN = {
    (0, 0): ((0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0), tuple(range(12))),
    (0, 1): ((0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0), tuple(range(12))),
    (1, 0): ((0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d), (
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51,
        0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1,
        0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
        0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
        0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
        0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2,
        0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa)),
    (1, 1): ((0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77), (
        0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07,
        0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09,
        0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
        0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
        0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
        0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
        0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
        0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
        0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
        0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2,
        0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa)),
}

MARKERS = {0xC0: 'SOF0', 0xC1: 'SOF1', 0xC2: 'SOF2', 0xC3: 'SOF3', 0xC4: 'DHT',
           0xC5: 'SOF5', 0xC6: 'SOF6', 0xC7: 'SOF7', 0xC8: 'JPG', 0xC9: 'SOF9',
           0xCA: 'SOF10', 0xCB: 'SOF11', 0xCC: 'DAC', 0xCD: 'SOF13', 0xCE: 'SOF14',
           0xCF: 'SOF15', 0xD8: 'SOI', 0xD9: 'EOI', 0xDA: 'SOS', 0xDB: 'DQT', 0xDC: 'DNL',
           0xDD: 'DRI', 0xDE: 'DHP', 0xDF: 'EXP', 0xFE: 'COM'}
for _i in range(16):
    MARKERS[0xE0 + _i] = 'APP%d' % _i
for _i in range(8):
    MARKERS[0xD0 + _i] = 'RST%d' % _i
SOF_KINDS = {0xC0: 'baseline', 0xC1: 'extended sequential', 0xC2: 'progressive',
             0xC3: 'lossless', 0xC5: 'differential sequential',
             0xC6: 'differential progressive', 0xC7: 'differential lossless',
             0xC9: 'extended sequential, arithmetic coding',
             0xCA: 'progressive, arithmetic coding', 0xCB: 'lossless, arithmetic coding',
             0xCD: 'differential sequential, arithmetic coding',
             0xCE: 'differential progressive, arithmetic coding',
             0xCF: 'differential lossless, arithmetic coding'}

# JPEGsnoop's lists (DbSigs.cpp, Signatures.inl)
IJG_PROGRAMS = ('GIMP', 'IrfanView', 'idImager', 'FastStone Image Viewer', 'NeatImage',
                'Paint.NET', 'Photomatix', 'XnView')
COMMENT_EDITORS = ('gd-jpeg', 'Photoshop', 'ACD Systems', 'AppleMark', 'PICResize',
                   'NeatImage')
NO_MAKERNOTE_OK = (('Apple', 'iPhone'), ('CoreLogic', 'SAMSUNG'), ('HTC', 'Hermes'),
                   ('MOTOROLA', ''), ('Nokia', ''), ('???', 'Treo*'),
                   ('Research In Motion', ''), ('RIM', ''), ('Sony Ericsson', ''),
                   ('SONY', 'DIGITALMAVICA'), ('SONY', 'CYBERSHOT'), ('SONY', 'CYBERSHOT U'),
                   ('SONY', 'MAVICA'), ('SONY ERICSSON', ''), ('Vivitar', ''))
ALWAYS_EDITED = (('Noritsu', ''), ('Mercury Peripherals Inc.', 'DigitalCam Pro'))

HONEST = [
    'These are indicators, not proof. The quantisation tables tell how the file was saved '
    'the last time, not whether its content is true: any program can write any tables, and '
    'metadata can be edited or removed with ordinary tools.',
    'A signature that matches a camera means that the tables are those of that camera, not '
    'that the file came out of it unchanged; many cameras and programs share tables. A file '
    'with no match is not therefore edited: the database (JPEGsnoop, 2018) does not know '
    'newer cameras and phones.',
    'Double compression is looked for on the 8 x 8 grid of the file: an image that was '
    'cropped (by other than a multiple of 8 pixels) or resized between two saves, or saved '
    'first at a higher quality than the last time, often shows no sign of it. A sign of it '
    'says that the whole image was saved as JPEG before; that alone is common (a photo '
    'opened and saved again) and no forgery.',
]


class JpegError(Exception):
    pass


# ---------------------------------------------------------------- structure

def _camera_name(m):
    """Make and model of a database camera; JPEGsnoop writes "???" for an
    unknown make, which is left out."""
    make = (m.get('make') or '').strip()
    model = (m.get('model') or '').strip()
    if not make or set(make) == {'?'}:
        return model or 'unknown camera'
    return ('%s %s' % (make, model)).strip()


def u16(b, i):
    return (b[i] << 8) | b[i + 1]


def scan_data_end(data, start):
    """the end of entropy coded data that starts at start: the next marker
    that is neither a stuffed byte (FF 00) nor a restart marker"""
    n = len(data)
    p = start
    while True:
        p = data.find(b'\xff', p)
        if p < 0 or p + 1 >= n:
            return n
        b = data[p + 1]
        if b == 0x00 or 0xD0 <= b <= 0xD7 or b == 0xFF:
            p += 1
            continue
        return p


def read_segments(data):
    """[(marker, offset of the FF, payload start, payload end, scan end)]:
    every marker segment in order; for SOS the scan's entropy coded data
    runs from payload end to scan end"""
    if len(data) < 4 or data[0] != 0xFF or data[1] != 0xD8:
        raise JpegError('not a JPEG file (no SOI marker at the start)')
    segs = [(0xD8, 0, 2, 2, None)]
    p = 2
    n = len(data)
    while p < n:
        if data[p] != 0xFF:
            # garbage between segments: look for the next marker
            q = data.find(b'\xff', p)
            if q < 0:
                break
            segs.append(('junk', p, p, q, None))
            p = q
            continue
        while p + 1 < n and data[p + 1] == 0xFF:
            p += 1           # fill bytes
        if p + 1 >= n:
            break
        m = data[p + 1]
        if m == 0xD9:
            segs.append((m, p, p + 2, p + 2, None))
            p += 2
            break
        if 0xD0 <= m <= 0xD7 or m == 0x01:
            segs.append((m, p, p + 2, p + 2, None))
            p += 2
            continue
        if p + 4 > n:
            raise JpegError('the file ends inside a marker')
        length = u16(data, p + 2)
        if length < 2 or p + 2 + length > n:
            raise JpegError('the %s segment at %d is cut off' % (MARKERS.get(m, '0x%02X' % m), p))
        start, end = p + 4, p + 2 + length
        if m == 0xDA:
            e = scan_data_end(data, end)
            segs.append((m, p, start, end, e))
            p = e
        else:
            segs.append((m, p, start, end, None))
            p = end
    return segs, p


def parse_dqt(payload):
    tables = []
    i = 0
    while i < len(payload):
        pq, tq = payload[i] >> 4, payload[i] & 15
        i += 1
        size = 128 if pq else 64
        if i + size > len(payload) or tq > 3:
            raise JpegError('a DQT segment is malformed')
        values = [0] * 64
        for k in range(64):
            if pq:
                v = (payload[i] << 8) | payload[i + 1]
                i += 2
            else:
                v = payload[i]
                i += 1
            values[ZIGZAG[k]] = v
        tables.append({'id': tq, 'precision': 16 if pq else 8, 'values': values})
    return tables


def parse_dht(payload):
    tables = []
    i = 0
    while i < len(payload):
        if i + 17 > len(payload):
            raise JpegError('a DHT segment is malformed')
        tc, th = payload[i] >> 4, payload[i] & 15
        counts = tuple(payload[i + 1:i + 17])
        total = sum(counts)
        symbols = tuple(payload[i + 17:i + 17 + total])
        if len(symbols) != total or tc > 1 or th > 3:
            raise JpegError('a DHT segment is malformed')
        tables.append({'class': tc, 'id': th, 'counts': counts, 'symbols': symbols})
        i += 17 + total
    return tables


def parse_sof(m, payload):
    if len(payload) < 6:
        raise JpegError('the frame header is cut off')
    precision = payload[0]
    height, width = u16(payload, 1), u16(payload, 3)
    nc = payload[5]
    if len(payload) < 6 + 3 * nc or nc == 0:
        raise JpegError('the frame header is cut off')
    comps = []
    for c in range(nc):
        cid, hv, tq = payload[6 + 3 * c:9 + 3 * c]
        comps.append({'id': cid, 'h': hv >> 4, 'v': hv & 15, 'tq': tq})
    if any(not (1 <= c['h'] <= 4 and 1 <= c['v'] <= 4) for c in comps):
        raise JpegError('invalid sampling factors in the frame header')
    return {'marker': MARKERS.get(m), 'kind': SOF_KINDS.get(m, 'unknown'),
            'progressive': m in (0xC2, 0xC6, 0xCA, 0xCE),
            'arithmetic': m >= 0xC9, 'lossless': m in (0xC3, 0xC7, 0xCB, 0xCF),
            'precision': precision, 'width': width, 'height': height, 'components': comps}


def parse_sos(payload):
    ns = payload[0]
    comps = []
    for c in range(ns):
        cid, t = payload[1 + 2 * c], payload[2 + 2 * c]
        comps.append({'id': cid, 'dc': t >> 4, 'ac': t & 15})
    ss, se, a = payload[1 + 2 * ns], payload[2 + 2 * ns], payload[3 + 2 * ns]
    return {'components': comps, 'ss': ss, 'se': se, 'ah': a >> 4, 'al': a & 15}


def subsampling_name(frame):
    comps = frame['components']
    if len(comps) == 1:
        return 'grayscale'
    if len(comps) != 3:
        return '%d components' % len(comps)
    y, cb, cr = comps
    if (cb['h'], cb['v']) != (cr['h'], cr['v']):
        return 'unusual (%dx%d, %dx%d, %dx%d)' % (y['h'], y['v'], cb['h'], cb['v'],
                                                  cr['h'], cr['v'])
    fh, fv = y['h'] / cb['h'], y['v'] / cb['v']
    names = {(1, 1): '4:4:4', (2, 1): '4:2:2', (2, 2): '4:2:0', (1, 2): '4:4:0',
             (4, 1): '4:1:1', (4, 2): '4:1:0'}
    return names.get((fh, fv), 'unusual (%dx%d, %dx%d, %dx%d)' % (
        y['h'], y['v'], cb['h'], cb['v'], cr['h'], cr['v']))


def jpegsnoop_css(frame):
    """the subsampling as JPEGsnoop writes it ("2x2", "Gray"), for a
    landscape or square image"""
    comps = frame['components']
    if len(comps) == 1:
        return 'Gray'
    if len(comps) != 3:
        return '?x?'
    hmax = max(c['h'] for c in comps)
    vmax = max(c['v'] for c in comps)
    cb = comps[1]
    fh, fv = hmax // cb['h'], vmax // cb['v']
    if frame['width'] < frame['height']:
        fh, fv = fv, fh
    return '%dx%d' % (fh, fv)


# ---------------------------------------------------------------- metadata

EXIF_TYPES = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8, 11: 4, 12: 8}
IFD0_TAGS = {0x010F: 'Make', 0x0110: 'Model', 0x0131: 'Software', 0x0132: 'DateTime',
             0x0112: 'Orientation', 0x013B: 'Artist', 0x8298: 'Copyright',
             0x010E: 'ImageDescription', 0x0100: 'ImageWidth', 0x0101: 'ImageLength',
             0x011A: 'XResolution', 0x011B: 'YResolution'}
EXIF_TAGS = {0x9003: 'DateTimeOriginal', 0x9004: 'DateTimeDigitized', 0x9290: 'SubSecTime',
             0xA002: 'PixelXDimension', 0xA003: 'PixelYDimension', 0x9000: 'ExifVersion',
             0xA430: 'CameraOwnerName', 0xA431: 'BodySerialNumber', 0xA434: 'LensModel',
             0x8827: 'ISOSpeedRatings', 0x829A: 'ExposureTime', 0x829D: 'FNumber',
             0x9010: 'OffsetTime', 0x9011: 'OffsetTimeOriginal'}
IFD1_TAGS = {0x0103: 'Compression', 0x0201: 'JPEGInterchangeFormat',
             0x0202: 'JPEGInterchangeFormatLength', 0x0100: 'ImageWidth', 0x0101: 'ImageLength'}


def parse_tiff(buf):
    """Exif (a TIFF structure): {'ifd0': {...}, 'exif': {...}, 'ifd1': {...},
    'makernote': its size or 0, 'gps': bool, 'thumbnail': bytes or None,
    'problems': [...]}"""
    out = {'ifd0': {}, 'exif': {}, 'ifd1': {}, 'makernote': 0, 'gps': False,
           'thumbnail': None, 'problems': []}
    if len(buf) < 8:
        out['problems'].append('Exif data too short')
        return out
    if buf[:2] == b'II':
        e = '<'
    elif buf[:2] == b'MM':
        e = '>'
    else:
        out['problems'].append('Exif data without a TIFF header')
        return out

    def rd(fmt, off):
        if off < 0 or off + struct.calcsize(e + fmt) > len(buf):
            raise JpegError('Exif data cut off')
        return struct.unpack_from(e + fmt, buf, off)

    def value(typ, count, field):
        """the value of an entry whose 4 byte value field is at field"""
        size = EXIF_TYPES.get(typ, 1) * count
        off = field if size <= 4 else rd('I', field)[0]
        if off + size > len(buf):
            raise JpegError('an Exif value is cut off')
        if typ == 2:
            return bytes(buf[off:off + count]).split(b'\x00')[0].decode('utf-8', 'replace').strip()
        if count < 1:
            return None
        if typ == 3:
            return rd('H', off)[0]
        if typ == 4:
            return rd('I', off)[0]
        if typ == 9:
            return rd('i', off)[0]
        if typ in (5, 10):
            a, b = rd('II' if typ == 5 else 'ii', off)
            return a / b if b else 0.0
        return None

    def ifd(off, names, into):
        """reads a directory: its entries (tag, type, count, field offset)
        and the offset of the next one"""
        if off <= 0 or off + 2 > len(buf):
            return 0, []
        n = rd('H', off)[0]
        if off + 2 + 12 * n > len(buf):
            out['problems'].append('an Exif directory is cut off')
            n = max(0, (len(buf) - off - 2) // 12)
        tags = []
        for k in range(n):
            eo = off + 2 + 12 * k
            tag, typ, count = rd('HHI', eo)
            tags.append((tag, typ, count, eo + 8))
            if tag in names:
                try:
                    into[names[tag]] = value(typ, count, eo + 8)
                except JpegError:
                    pass
        end = off + 2 + 12 * n
        nxt = rd('I', end)[0] if end + 4 <= len(buf) else 0
        return nxt, tags

    try:
        nxt, tags = ifd(rd('I', 4)[0], IFD0_TAGS, out['ifd0'])
        for tag, typ, count, field in tags:
            if tag == 0x8769:
                _, etags = ifd(rd('I', field)[0], EXIF_TAGS, out['exif'])
                for t2, typ2, count2, field2 in etags:
                    if t2 == 0x927C:
                        out['makernote'] = count2
            elif tag == 0x8825:
                out['gps'] = True
        if nxt:
            ifd(nxt, IFD1_TAGS, out['ifd1'])
            t = out['ifd1']
            off, size = t.get('JPEGInterchangeFormat'), t.get('JPEGInterchangeFormatLength')
            if isinstance(off, int) and isinstance(size, int) and size > 0:
                if off + size <= len(buf):
                    out['thumbnail'] = bytes(buf[off:off + size])
                else:
                    out['problems'].append('the Exif thumbnail is cut off')
    except JpegError as ex:
        out['problems'].append(str(ex))
    return out


def parse_ducky(payload):
    """APP12 "Ducky" (Photoshop's Save for Web): its quality, 0 to 100"""
    p = 6
    while p + 4 <= len(payload):
        tag, size = u16(payload, p), u16(payload, p + 2)
        if tag == 0:
            break
        data = payload[p + 4:p + 4 + size]
        if tag == 1 and size >= 1:
            return int.from_bytes(data, 'big')
        p += 4 + size
    return None


def parse_photoshop(payload):
    """APP13 "Photoshop 3.0": the 8BIM resources' ids; the Save As quality
    (0x0406, as JPEGsnoop decodes it) and whether there is IPTC (0x0404)"""
    out = {'resources': [], 'quality': None, 'format': None, 'iptc': False, 'thumbnail': False}
    p = payload.find(b'\x00') + 1
    n = len(payload)
    while p + 12 <= n and payload[p:p + 4] == b'8BIM':
        rid = u16(payload, p + 4)
        name_len = payload[p + 6]
        q = p + 7 + name_len
        if (name_len + 1) % 2:
            q += 1
        if q + 4 > n:
            break
        size = struct.unpack_from('>I', payload, q)[0]
        data = payload[q + 4:q + 4 + size]
        out['resources'].append(rid)
        if rid == 0x0406 and len(data) >= 2:
            v = struct.unpack_from('>h', data, 0)[0]
            if -3 <= v <= 8:
                out['quality'] = v + 4
            if len(data) >= 4:
                out['format'] = {0: 'standard', 1: 'optimized', 0x101: 'progressive'}.get(
                    u16(data, 2), 'unknown')
        elif rid == 0x0404:
            out['iptc'] = True
        elif rid in (0x0409, 0x040C):
            out['thumbnail'] = True
        p = q + 4 + size + (size % 2)
    return out


def xmp_fields(text):
    out = {}
    for key, pat in (('CreatorTool', r'xmp:CreatorTool(?:="([^"]*)"|>([^<]*)<)'),
                     ('DigitalSourceType', r'DigitalSourceType(?:="([^"]*)"|>([^<]*)<)'),
                     ('softwareAgent', r'stEvt:softwareAgent(?:="([^"]*)"|>([^<]*)<)'),
                     ('History', r'(xmpMM:History)'),
                     ('DocumentID', r'xmpMM:DocumentID(?:="([^"]*)"|>([^<]*)<)'),
                     ('ModifyDate', r'xmp:ModifyDate(?:="([^"]*)"|>([^<]*)<)')):
        found = []
        for m in re.finditer(pat, text):
            v = next((g for g in m.groups() if g), '')
            v = v.strip()
            if v and v not in found:
                found.append(v)
        if found:
            out[key] = found
    return out


def parse(data):
    """The structure and metadata of a JPEG file (bytes): a dict, see
    analyze (). Raises JpegError for what is not a JPEG file."""
    segs, end = read_segments(data)
    info = {'segments': [], 'frame': None, 'tables': [], 'huffman': [], 'scans': [],
            'restart_interval': 0, 'app': [], 'comments': [], 'jfif': None, 'exif': None,
            'xmp': None, 'icc': None, 'photoshop': None, 'adobe': None, 'mpf': False,
            'c2pa': False, 'ducky': None, 'trailer': len(data) - end, 'events': [],
            'problems': []}
    xmp_text = b''
    icc_chunks = {}
    for seg in segs:
        m, off, s, e, scan_end = seg
        if m == 'junk':
            info['problems'].append('%d bytes of junk at %d' % (e - s, s))
            continue
        name = MARKERS.get(m, '0x%02X' % m)
        payload = data[s:e]
        info['segments'].append({'marker': name, 'offset': off, 'length': e - off})
        if m == 0xDB:
            for t in parse_dqt(payload):
                info['tables'].append(t)
                info['events'].append(('dqt', t))
        elif m == 0xC4:
            for t in parse_dht(payload):
                info['huffman'].append(t)
                info['events'].append(('dht', t))
        elif m in SOF_KINDS:
            if info['frame'] is None:
                info['frame'] = parse_sof(m, payload)
            else:
                info['problems'].append('a second frame header (%s)' % name)
        elif m == 0xDD:
            if len(payload) >= 2:
                info['restart_interval'] = u16(payload, 0)
                info['events'].append(('dri', info['restart_interval']))
        elif m == 0xDA:
            sos = parse_sos(payload)
            sos['data'] = (e, scan_end)
            info['scans'].append(sos)
            info['events'].append(('sos', sos))
        elif m == 0xFE:
            info['comments'].append(payload.decode('utf-8', 'replace').rstrip('\x00').strip())
        elif 0xE0 <= m <= 0xEF:
            ident = payload[:payload.find(b'\x00')] if b'\x00' in payload[:40] else payload[:12]
            ident = ident.decode('latin-1', 'replace')
            info['app'].append({'marker': name, 'id': ident, 'length': e - off})
            if m == 0xE0 and payload[:5] == b'JFIF\x00' and len(payload) >= 14:
                info['jfif'] = {'version': '%d.%02d' % (payload[5], payload[6]),
                                'units': payload[7], 'x_density': u16(payload, 8),
                                'y_density': u16(payload, 10),
                                'thumbnail': '%dx%d' % (payload[12], payload[13])}
            elif m == 0xE1 and payload[:6] == b'Exif\x00\x00':
                if info['exif'] is None:
                    info['exif'] = parse_tiff(payload[6:])
            elif m == 0xE1 and payload.startswith(b'http://ns.adobe.com/xap/1.0/\x00'):
                xmp_text += payload[29:]
            elif m == 0xE1 and payload.startswith(b'http://ns.adobe.com/xmp/extension/\x00'):
                xmp_text += payload[35 + 40:]
            elif m == 0xE2 and payload[:12] == b'ICC_PROFILE\x00' and len(payload) > 14:
                icc_chunks[payload[12]] = payload[14:]
            elif m == 0xE2 and payload[:4] == b'MPF\x00':
                info['mpf'] = True
            elif m == 0xEC and payload[:6] == b'Ducky\x00':
                info['ducky'] = parse_ducky(payload)
            elif m == 0xED and payload.startswith(b'Photoshop 3.0\x00'):
                info['photoshop'] = parse_photoshop(payload)
            elif m == 0xEE and payload[:5] == b'Adobe' and len(payload) >= 12:
                info['adobe'] = {'version': u16(payload, 5), 'flags0': u16(payload, 7),
                                 'flags1': u16(payload, 9), 'transform': payload[11]}
            elif m == 0xEB and b'jumb' in payload[:40] or (m == 0xEB and b'c2pa' in payload[:64]):
                info['c2pa'] = True
    if xmp_text:
        info['xmp'] = xmp_fields(xmp_text.decode('utf-8', 'replace'))
    if icc_chunks:
        icc = b''.join(icc_chunks[k] for k in sorted(icc_chunks))
        desc = None
        m = re.search(rb'desc\x00\x00\x00\x00\x00\x00\x00(.)(.{1,80}?)\x00', icc, re.S)
        if m:
            desc = m.group(2).decode('latin-1', 'replace')
        info['icc'] = {'size': len(icc), 'description': desc}
    if info['frame'] is None:
        raise JpegError('no frame header (SOF): not an image')
    return info


# ---------------------------------------------------------------- quality

def quality_scaling(q):
    """libjpeg's jpeg_quality_scaling (): the percentage the base tables
    are scaled by for quality q (1 to 100)"""
    q = min(max(int(q), 1), 100)
    return 5000 // q if q < 50 else 200 - 2 * q


def scaled_table(base, q, baseline=True):
    """libjpeg's jpeg_add_quant_table () with the scaling of quality q"""
    s = quality_scaling(q)
    out = []
    for b in base:
        t = (b * s + 50) // 100
        t = min(max(t, 1), 32767)
        if baseline:
            t = min(t, 255)
        out.append(t)
    return out


def ijg_tables(q, baseline=True):
    return scaled_table(ANNEX_K_LUMA, q, baseline), scaled_table(ANNEX_K_CHROMA, q, baseline)


def mean_abs(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / 64.0


def table_level(values):
    """Sherloq's "level" of a table: 100 % for all ones, 0 for all 255
    (the mean of its AC entries)"""
    return (1 - (sum(values[1:]) / 63.0 - 1) / 254) * 100


def exact_family_match(luma, chroma):
    """(family index, quality, baseline) if the tables are one of the
    base tables scaled by libjpeg's rule at some quality, else None.
    Grayscale files have only luma."""
    for fi, (name, bl, bc) in enumerate(BASE_TABLES):
        bc = bc or bl
        for baseline in (True, False):
            for q in range(100, 0, -1):
                if scaled_table(bl, q, baseline) != luma:
                    continue
                if chroma is None or scaled_table(bc, q, baseline) == chroma:
                    return fi, q, baseline
    return None


def sherloq_estimate(luma, chroma):
    """Sherloq's estimate (tools/jpeg/quality.py): the IJG quality whose
    tables are nearest (mean absolute difference, luma + 2 x chroma, / 3),
    minus that distance, rounded; and the distance"""
    best, bestq = None, 0
    for q in range(0, 101):
        lu, ch = ijg_tables(max(q, 1))
        if q == 0:
            lu, ch = ijg_tables(1)
        d = (mean_abs(luma, lu) + 2 * mean_abs(chroma if chroma else luma,
                                                  ch if chroma else lu)) / 3
        if best is None or d < best:
            best, bestq = d, q
    if best == 0:
        return bestq, 0.0
    return max(int(round(bestq - best)), 1), best


def quality_of(info):
    """the quality of the last save: {'estimate', 'exact', 'family',
    'baseline', 'deviation', 'method', 'per_table'}"""
    tables = {}
    for t in info['tables']:
        tables[t['id']] = t['values']    # (a later DQT replaces an earlier one)
    comps = info['frame']['components']
    luma = tables.get(comps[0]['tq'])
    chroma = tables.get(comps[1]['tq']) if len(comps) >= 3 else None
    if luma is None:
        return {'estimate': None, 'exact': False, 'method': 'no quantisation table for luma',
                'per_table': []}
    per = []
    for tid in sorted(tables):
        v = tables[tid]
        base = ANNEX_K_LUMA if v is luma else ANNEX_K_CHROMA
        best = min(range(1, 101), key=lambda q: (mean_abs(v, scaled_table(base, q)), -q))
        exact = scaled_table(base, best) == v or scaled_table(base, best, False) == v
        per.append({'id': tid, 'closest_ijg': best, 'exact': exact,
                    'deviation': mean_abs(v, scaled_table(base, best)),
                    'level': table_level(v)})
    m = exact_family_match(luma, chroma)
    if m:
        fi, q, baseline = m
        return {'estimate': q, 'exact': True, 'family': BASE_TABLES[fi][0], 'family_index': fi,
                'baseline': baseline, 'deviation': 0.0,
                'method': 'the %s tables scaled for quality %d (libjpeg\'s rule)' % (
                    'IJG' if fi == 0 else BASE_TABLES[fi][0], q),
                'per_table': per}
    q, d = sherloq_estimate(luma, chroma)
    return {'estimate': q, 'exact': False, 'family': None, 'baseline': None, 'deviation': d,
            'method': 'not a standard table: the nearest IJG quality less the mean deviation '
                      '(%.2f), as Sherloq estimates it' % d,
            'per_table': per}


def huffman_kind(info):
    """'standard' if every Huffman table is one of Annex K.3 (libjpeg without
    optimisation, many cameras), 'optimized' if none is, else 'mixed'; None
    without tables"""
    if not info['huffman']:
        return None
    std = [t['counts'] == STD_HUFFMAN.get((t['class'], min(t['id'], 1)), ((), ()))[0] and
           t['symbols'] == STD_HUFFMAN.get((t['class'], min(t['id'], 1)))[1]
           for t in info['huffman']]
    return 'standard' if all(std) else 'optimized' if not any(std) else 'mixed'


# ---------------------------------------------------------------- signatures

_SIGNATURES = None


def load_signatures(path=None):
    """JPEGsnoop's database (jpegsnoop-signatures.tsv next to this file)"""
    global _SIGNATURES
    if path is None and _SIGNATURES is not None:
        return _SIGNATURES
    p = path or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             'jpegsnoop-signatures.tsv')
    rows = []
    try:
        with open(p, encoding='utf-8') as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                v = line.rstrip('\n').split('\t')
                if len(v) != 9:
                    continue
                rows.append({'kind': v[0], 'make': v[1], 'model': v[2], 'quality': v[3],
                             'sig': v[4], 'sigrot': v[5], 'subsampling': v[6],
                             'software': v[7], 'name': v[8]})
    except OSError:
        rows = []
    if path is None:
        _SIGNATURES = rows
    return rows


def jpegsnoop_signature(tables, rotate=False):
    """JPEGsnoop's compression signature (DB_SIG_VER 0x01) of tables, a list
    of 4 (natural order values or None)"""
    s = 'JPEGsnoop'
    for i, t in enumerate(tables):
        if t is None:
            continue
        s += '*DQT%u,' % i
        s += ''.join('%03u,' % t[(k % 8) * 8 + k // 8 if rotate else k] for k in range(64))
    s += '*END'
    return '01' + hashlib.md5(s.encode('ascii')).hexdigest().upper()[2:]


def signature_tables(info):
    tables = [None] * 4
    for t in info['tables']:
        tables[t['id']] = t['values']
    return tables


def match_signatures(info, exif, db=None):
    """the database entries whose signature is the file's, with whether
    the camera's make and model (or software) are those of the Exif data"""
    db = load_signatures() if db is None else db
    tables = signature_tables(info)
    if not any(tables):
        return {'signature': None, 'rotated': None, 'matches': []}
    sig, rot = jpegsnoop_signature(tables), jpegsnoop_signature(tables, True)
    css = jpegsnoop_css(info['frame'])
    make = (exif or {}).get('ifd0', {}).get('Make') or ''
    model = (exif or {}).get('ifd0', {}).get('Model') or ''
    software = (exif or {}).get('ifd0', {}).get('Software') or ''
    matches = []
    for r in db:
        if r['sig'] not in (sig, rot) and r['sigrot'] not in (sig, rot):
            continue
        entry = dict(r)
        entry['subsampling_matches'] = r['kind'] == 'sw' or r['subsampling'] == css
        entry['make_model_matches'] = (r['kind'] == 'cam' and bool(make) and
                                       r['make'] == make and r['model'] == model)
        entry['software_matches'] = (r['kind'] == 'sw' and bool(r['software']) and
                                     r['software'] in software)
        matches.append(entry)
    return {'signature': sig, 'rotated': rot, 'subsampling': css, 'matches': matches}


def _listed(pairs, make, model):
    for mk, md in pairs:
        if mk != make:
            continue
        if not md or md == model or (md.endswith('*') and model.startswith(md[:-1])):
            return True
    return False


def assessment(info, sig):
    """JPEGsnoop's assessment (CompareSignature): class 1 "processed/edited",
    2 "high probability of being processed/edited", 3 "high probability of
    being original", 4 "uncertain"; with the reasons"""
    exif = info.get('exif') or {}
    ifd0 = exif.get('ifd0', {})
    make, model = ifd0.get('Make') or '', ifd0.get('Model') or ''
    software = ifd0.get('Software') or ''
    db = load_signatures()
    reasons = []
    definite = likely = False
    ps = (info.get('photoshop') or {}).get('quality')
    if ps or info.get('ducky'):
        definite = True
        reasons.append('Photoshop wrote its quality setting (%s)' % (
            'Save As %d of 12' % ps if ps else 'Save for Web %d' % info['ducky']))
    if not (make or model):
        definite = True
        reasons.append('no camera make and model in Exif' if info.get('exif') else 'no Exif data')
    for c in info['comments']:
        for s in COMMENT_EDITORS:
            if s in c:
                definite = True
                reasons.append('the comment names software: "%s"' % c[:60])
                break
    known_sw = None
    if software:
        for r in db:
            if r['kind'] == 'sw' and r['software'] and r['software'] in software:
                known_sw = r['name']
                break
        if known_sw:
            definite = True
            reasons.append('the Exif Software field names an editor: %s' % known_sw)
    if _listed(ALWAYS_EDITED, make, model):
        definite = True
        reasons.append('%s %s marks processed files' % (make, model))
    if not exif.get('makernote') and not _listed(NO_MAKERNOTE_OK, make, model):
        likely = True
        reasons.append('no maker notes in Exif (cameras write them; most editors drop them)')
    cam_match = any(m['kind'] == 'cam' and m['make_model_matches'] and
                    m['subsampling_matches'] for m in sig['matches'])
    if definite:
        cls = 1
    elif likely:
        cls = 2
    elif cam_match:
        cls = 3
        reasons.append('the tables match this camera (%s %s) in the database' % (make, model))
        if software:
            reasons.append('the Exif Software field is set ("%s"), often the firmware' % software)
    else:
        cls = 4
        reasons.append('the Exif data look original, but no signature of this make and '
                       'model is in the database')
    text = {1: 'processed or edited', 2: 'high probability of being processed or edited',
            3: 'high probability of being original', 4: 'uncertain whether processed or '
            'original'}[cls]
    return {'class': cls, 'text': text, 'reasons': reasons}


# ---------------------------------------------------------------- entropy decoding

class Huffman:
    """a lookup of 16 bits: entry (length << 8) | symbol, 0 for no code"""
    __slots__ = ('lookup',)

    def __init__(self, counts, symbols):
        lookup = [0] * 65536
        code = 0
        k = 0
        for length in range(1, 17):
            for _ in range(counts[length - 1]):
                if k >= len(symbols) or code >= (1 << length):
                    raise JpegError('a Huffman table is invalid')
                shift = 16 - length
                a, b = code << shift, (code + 1) << shift
                lookup[a:b] = [(length << 8) | symbols[k]] * (b - a)
                k += 1
                code += 1
            code <<= 1
        self.lookup = lookup


class Bits:
    """the bits of one restart interval's entropy coded data (bytes
    unstuffed)"""
    __slots__ = ('data', 'pos', 'acc', 'n', 'over')

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.acc = 0
        self.n = 0
        self.over = 0

    def fill(self):
        d = self.data
        p = self.pos
        chunk = d[p:p + 4]
        if len(chunk) < 4:
            self.over += 4 - len(chunk)
            chunk = chunk + b'\x00' * (4 - len(chunk))
        self.acc = ((self.acc & ((1 << self.n) - 1)) << 32) | int.from_bytes(chunk, 'big')
        self.pos = p + 4
        self.n += 32

    def huff(self, h):
        if self.n < 16:
            self.fill()
        e = h.lookup[(self.acc >> (self.n - 16)) & 0xFFFF]
        if not e:
            raise JpegError('a Huffman code that is not in the table')
        self.n -= e >> 8
        return e & 0xFF

    def bits(self, k):
        if k == 0:
            return 0
        if self.n < k:
            self.fill()
        self.n -= k
        return (self.acc >> self.n) & ((1 << k) - 1)

    def extend(self, s):
        if s == 0:
            return 0
        if self.n < s:
            self.fill()
        self.n -= s
        v = (self.acc >> self.n) & ((1 << s) - 1)
        return v if v >> (s - 1) else v - (1 << s) + 1


def restart_segments(data, start, end):
    """the entropy coded data of a scan, cut at its restart markers and
    unstuffed"""
    segs = []
    p = start
    q = start
    while True:
        q = data.find(b'\xff', q, end)
        if q < 0 or q + 1 >= end:
            segs.append(data[p:end])
            break
        b = data[q + 1]
        if 0xD0 <= b <= 0xD7:
            segs.append(data[p:q])
            p = q + 2
            q = p
        else:
            q += 2 if b == 0 else 1
    return [s.replace(b'\xff\x00', b'\xff') for s in segs]


def component_geometry(frame):
    """per component: blocks across and down (as libjpeg's width_in_blocks),
    and the MCU grid of interleaved scans"""
    X, Y = frame['width'], frame['height']
    comps = frame['components']
    hmax = max(c['h'] for c in comps)
    vmax = max(c['v'] for c in comps)
    geo = []
    for c in comps:
        cw = -(-X * c['h'] // hmax)
        ch = -(-Y * c['v'] // vmax)
        geo.append({'bw': -(-cw // 8), 'bh': -(-ch // 8)})
    mcux = -(-X // (8 * hmax))
    mcuy = -(-Y // (8 * vmax))
    return geo, mcux, mcuy


def decode_luma(data, info, max_blocks=30000, time_limit=None):
    """The quantised DCT coefficients of the first component (luma) of the
    file, for its first rows of 8 x 8 blocks (at most max_blocks blocks,
    whole rows): {'coefs': array of 64 per block (natural order), 'bw':
    blocks across, 'rows', 'blocks', 'complete': every row read,
    'total_rows', 'seconds'}. Sequential and progressive Huffman coded
    files (T.81 F.2 and G.2); scans without luma are skipped, interleaved
    ones decoded only as far as the rows asked for. Raises JpegError for
    what is not decoded here (arithmetic coding, lossless, hierarchical)
    or cannot be (damaged data), and when time_limit seconds are over."""
    t0 = time.monotonic()
    frame = info['frame']
    if frame['arithmetic'] or frame['lossless'] or frame['marker'] in ('SOF5', 'SOF6', 'SOF7'):
        raise JpegError('%s JPEG files are not decoded here' % frame['kind'])
    if frame['width'] == 0 or frame['height'] == 0:
        raise JpegError('the image size is in a DNL marker, not decoded here')
    geo, mcux, mcuy = component_geometry(frame)
    comps = frame['components']
    index_of = {c['id']: i for i, c in enumerate(comps)}
    bw = geo[0]['bw']
    rows = min(geo[0]['bh'], max(1, max_blocks // max(bw, 1)))
    coefs = array.array('i', bytes(4 * 64 * rows * bw))
    zz = ZIGZAG
    # the standard tables until a DHT replaces them (Motion JPEG frames
    # leave them out, as libjpeg allows)
    huff = {key: Huffman(*t) for key, t in STD_HUFFMAN.items()}
    restart = 0
    progressive = frame['progressive']

    for kind, ev in info['events']:
        if kind == 'dht':
            huff[(ev['class'], ev['id'])] = Huffman(ev['counts'], ev['symbols'])
            continue
        if kind == 'dri':
            restart = ev
            continue
        if kind != 'sos':
            continue
        sc = []
        for c in ev['components']:
            if c['id'] not in index_of:
                raise JpegError('a scan names a component the frame does not have')
            sc.append(index_of[c['id']])
        if 0 not in sc:
            continue
        ss, se, ah, al = ev['ss'], ev['se'], ev['ah'], ev['al']
        if not progressive:
            ss, se, ah, al = 0, 63, 0, 0
        dc_only = progressive and ss == 0
        try:
            dcs = [huff[(0, c['dc'])] if not progressive or (ss == 0 and ah == 0) else None
                   for c in ev['components']]
            acs = [huff[(1, c['ac'])] if not dc_only else None for c in ev['components']]
        except KeyError:
            raise JpegError('a scan uses a Huffman table the file does not define')
        state = _ScanState(restart_segments(data, ev['data'][0], ev['data'][1]), len(comps),
                           restart)
        if len(sc) == 1:
            # one component: its blocks in raster order, an MCU each
            dct, act = dcs[0], acs[0]
            for n in range(rows * bw):
                state.next_mcu()
                bits = state.bits
                base = 64 * n
                if not progressive:
                    _sequential_block(bits, dct, act, coefs, base, state.pred, 0, zz)
                elif dc_only:
                    if ah == 0:
                        state.pred[0] += bits.extend(bits.huff(dct))
                        coefs[base] = state.pred[0] * (1 << al)
                    elif bits.bits(1):
                        coefs[base] |= 1 << al
                elif ah == 0:
                    state.eobrun = _ac_first(bits, act, coefs, base, ss, se, al,
                                             state.eobrun, zz)
                else:
                    state.eobrun = _ac_refine(bits, act, coefs, base, ss, se, al,
                                              state.eobrun, zz)
                if (n & 511) == 0:
                    state.check(t0, time_limit)
            state.check(t0, time_limit)
            continue
        # interleaved (sequential, or progressive DC): MCU by MCU
        hv = [(comps[ci]['h'], comps[ci]['v']) for ci in sc]
        scratch = array.array('i', bytes(4 * 64))
        for my in range(min(mcuy, -(-rows // comps[0]['v']))):
            for mx in range(mcux):
                state.next_mcu()
                bits = state.bits
                for j, ci in enumerate(sc):
                    h, v = hv[j]
                    for by in range(v):
                        for bx in range(h):
                            x = mx * h + bx
                            y = my * v + by
                            if ci == 0 and x < bw and y < rows:
                                out, base = coefs, 64 * (y * bw + x)
                            else:
                                out, base = scratch, 0
                            if not dc_only:
                                _sequential_block(bits, dcs[j], acs[j], out, base, state.pred,
                                                  ci, zz)
                            elif ah == 0:
                                state.pred[ci] += bits.extend(bits.huff(dcs[j]))
                                out[base] = state.pred[ci] * (1 << al)
                            elif bits.bits(1):
                                out[base] |= 1 << al
            state.check(t0, time_limit)
    return {'coefs': coefs, 'bw': bw, 'rows': rows, 'blocks': rows * bw,
            'complete': rows == geo[0]['bh'], 'total_rows': geo[0]['bh'],
            'seconds': time.monotonic() - t0}


class _ScanState:
    """where a scan is: its restart intervals, the DC predictions, the
    end-of-band run"""

    def __init__(self, segments, ncomps, restart):
        self.segments = segments or [b'']
        self.seg = 0
        self.bits = Bits(self.segments[0])
        self.pred = [0] * ncomps
        self.eobrun = 0
        self.restart = restart
        self.mcus = 0

    def next_mcu(self):
        if self.restart and self.mcus and self.mcus % self.restart == 0:
            self.seg += 1
            self.bits = Bits(self.segments[self.seg] if self.seg < len(self.segments) else b'')
            for i in range(len(self.pred)):
                self.pred[i] = 0
            self.eobrun = 0
        self.mcus += 1

    def check(self, t0, limit):
        if self.bits.over > 8:
            raise JpegError('the scan data end early (a damaged or cut off file)')
        if limit is not None and time.monotonic() - t0 > limit:
            raise JpegError('decoding took longer than %g seconds' % limit)


def _sequential_block(bits, dct, act, coefs, base, pred, ci, zz):
    """a block of a sequential scan (T.81 F.2.2): DC difference, then AC"""
    pred[ci] += bits.extend(bits.huff(dct))
    coefs[base] = pred[ci]
    k = 1
    while k < 64:
        rs = bits.huff(act)
        r, s = rs >> 4, rs & 15
        if s:
            k += r
            if k > 63:
                raise JpegError('corrupt data (a run past the end of a block)')
            coefs[base + zz[k]] = bits.extend(s)
            k += 1
        elif r == 15:
            k += 16
        else:
            break


def _ac_first(bits, act, coefs, base, ss, se, al, eobrun, zz):
    """a block of a progressive AC first scan (T.81 G.1.2.2)"""
    if eobrun:
        return eobrun - 1
    k = ss
    while k <= se:
        rs = bits.huff(act)
        r, s = rs >> 4, rs & 15
        if s:
            k += r
            if k > 63:
                raise JpegError('corrupt data (a run past the block)')
            coefs[base + zz[k]] = bits.extend(s) * (1 << al)
            k += 1
        else:
            if r < 15:
                eobrun = (1 << r) - 1
                if r:
                    eobrun += bits.bits(r)
                return eobrun
            k += 16
    return 0


def _ac_refine(bits, act, coefs, base, ss, se, al, eobrun, zz):
    """a block of a progressive AC refinement scan (T.81 G.1.2.3, as
    libjpeg's decode_mcu_AC_refine)"""
    p1 = 1 << al
    m1 = -1 << al
    k = ss
    if eobrun == 0:
        while k <= se:
            rs = bits.huff(act)
            r, s = rs >> 4, rs & 15
            if s:
                s = p1 if bits.bits(1) else m1
            else:
                if r != 15:
                    eobrun = 1 << r
                    if r:
                        eobrun += bits.bits(r)
                    break
            while k <= se:
                z = base + zz[k]
                c = coefs[z]
                if c != 0:
                    if bits.bits(1):
                        if (c & p1) == 0:
                            coefs[z] = c + p1 if c >= 0 else c + m1
                else:
                    r -= 1
                    if r < 0:
                        break
                k += 1
            if s:
                if k > 63:
                    raise JpegError('corrupt data (a run past the block)')
                coefs[base + zz[k]] = s
            k += 1
    if eobrun > 0:
        while k <= se:
            z = base + zz[k]
            c = coefs[z]
            if c != 0:
                if bits.bits(1):
                    if (c & p1) == 0:
                        coefs[z] = c + p1 if c >= 0 else c + m1
            k += 1
        eobrun -= 1
    return eobrun


# ---------------------------------------------------------------- double quantisation

SIGMA = 0.5      # the rounding noise between two saves, in DCT units
EPS = 1e-4       # a floor for every bin (clipped pixels, outliers)
DQ_FREQS = 12    # the AC frequencies looked at, in zigzag order from 1
DETECT = 0.1     # nats per block: a frequency counts as double quantised
WEAK = 0.03


def _phi(x):
    return 0.5 * math.erfc(-x / math.sqrt(2.0))


def histogram_abs(coefs, nblocks, index, vmax_cap=60):
    """counts of |coefficient| = 0 .. V (V holds the rest) at a natural
    index, over nblocks blocks; V from the 99.5th percentile, 4 to 60"""
    counts = {}
    for b in range(nblocks):
        v = coefs[64 * b + index]
        if v < 0:
            v = -v
        counts[v] = counts.get(v, 0) + 1
    total = nblocks
    seen = 0
    V = 0
    for v in sorted(counts):
        seen += counts[v]
        V = v
        if seen >= 0.995 * total:
            break
    V = min(max(V, 4), vmax_cap)
    h = [0] * (V + 1)
    for v, c in counts.items():
        h[min(v, V)] += c
    return h


def _masses(width, count, lam, gam, sub):
    """the envelope's masses on bins of the given width: bin 0 is [0,
    width / 2], bin n is [(n - 1/2) width, (n + 1/2) width]; count bins
    and the rest; normalised. The envelope: exp(-lam t) (1 + t)^-gam."""
    out = []
    total = 0.0
    for n in range(count):
        lo = 0.0 if n == 0 else (n - 0.5) * width
        hi = (n + 0.5) * width
        step = (hi - lo) / sub
        s = 0.0
        for j in range(sub):
            t = lo + (j + 0.5) * step
            s += math.exp(-lam * t - gam * math.log1p(t))
        s *= step
        out.append(s)
        total += s
    # the rest, coarsely: up to 8 times further
    lo = (count - 0.5) * width
    rest = 0.0
    step = width * 2.0
    t = lo + step / 2
    for _ in range(4 * count):
        rest += math.exp(-lam * t - gam * math.log1p(t)) * step
        t += step
    total += rest
    return [m / total for m in out], rest / total


def _single_pmf(q2, V, lam, gam):
    m, rest = _masses(q2, V, lam, gam, 6)
    return m + [max(rest, 0.0)]


def _transfer(q1, q2, V, sigma=SIGMA):
    """for the first quantisation step q1: [(u, [(n, weight), ...])], the
    probability that |u| q1 plus rounding noise lands in |v| = n at step
    q2 (n = V holds the rest)"""
    U = int(math.ceil((V + 2) * q2 / q1)) + 3
    cols = []
    for u in range(U + 1):
        c = u * q1
        w = []
        acc = 0.0
        lo_n = max(0, int((c - 4 * sigma) / q2 - 1))
        hi_n = int((c + 4 * sigma) / q2 + 2)
        for n in range(lo_n, hi_n + 1):
            p = _phi(((n + 0.5) * q2 - c) / sigma) - _phi(((n - 0.5) * q2 - c) / sigma)
            if n:
                p += _phi(((-n + 0.5) * q2 - c) / sigma) - _phi(((-n - 0.5) * q2 - c) / sigma)
            if p > 1e-12:
                w.append((min(n, V), p))
                acc += p
        cols.append((u, w))
    return cols


def _double_pmf(q1, V, lam, gam, cols):
    m, rest = _masses(q1, len(cols), lam, gam, 4)
    p = [0.0] * (V + 1)
    for (u, w), mu in zip(cols, m):
        for n, x in w:
            p[n] += mu * x
    p[V] += rest
    return p


def _loglik(h, p):
    k = len(p)
    s = 0.0
    for c, x in zip(h, p):
        if c:
            s += c * math.log((1 - EPS) * max(x, 0.0) + EPS / k)
    return s


def _nelder_mead(f, x0, step=(0.7, 1.0), iters=80):
    pts = [list(x0), [x0[0] + step[0], x0[1]], [x0[0], x0[1] + step[1]]]
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = sorted(range(3), key=lambda i: vals[i])
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        if abs(vals[2] - vals[0]) < 1e-4:
            break
        cx = [(pts[0][d] + pts[1][d]) / 2 for d in range(2)]
        xr = [cx[d] + (cx[d] - pts[2][d]) for d in range(2)]
        fr = f(xr)
        if fr < vals[0]:
            xe = [cx[d] + 2 * (cx[d] - pts[2][d]) for d in range(2)]
            fe = f(xe)
            if fe < fr:
                pts[2], vals[2] = xe, fe
            else:
                pts[2], vals[2] = xr, fr
        elif fr < vals[1]:
            pts[2], vals[2] = xr, fr
        else:
            xc = [cx[d] + 0.5 * (pts[2][d] - cx[d]) for d in range(2)]
            fc = f(xc)
            if fc < vals[2]:
                pts[2], vals[2] = xc, fc
            else:
                for i in (1, 2):
                    pts[i] = [pts[0][d] + 0.5 * (pts[i][d] - pts[0][d]) for d in range(2)]
                    vals[i] = f(pts[i])
    i = min(range(3), key=lambda j: vals[j])
    return pts[i], vals[i]


def _params(x):
    return math.exp(x[0]), math.exp(x[1])


def analyze_frequency(h, q2, max_q1=None):
    """The double quantisation test of one frequency's histogram h (of
    |v|, see histogram_abs) at the last step q2: {'q1': the first step that
    fits best, 'llr': log likelihood of it over a single save, 'single':
    log likelihood of a single save}. q1 is looked for from q2 + 1 up
    (a first step at most q2 leaves no pattern that can be told apart
    reliably)."""
    V = len(h) - 1
    n = sum(h)
    mean = sum(i * c for i, c in enumerate(h)) / max(n, 1) * q2 + 0.3
    fs = lambda x: -_loglik(h, _single_pmf(q2, V, *_params(x)))  # noqa: E731
    best_x, best_f = None, None
    for g0 in (math.log(0.05), math.log(1.5)):
        x, f = _nelder_mead(fs, [math.log(1.0 / mean), g0])
        if best_f is None or f < best_f:
            best_x, best_f = x, f
    single = -best_f
    lam, gam = _params(best_x)
    top = max_q1 or min(8 * q2, q2 + 40, 64)
    # the candidates with the envelope of the single fit (the coefficients
    # before the first save), then the best three refitted
    cands = []
    trans = {}
    for q1 in range(q2 + 1, top + 1):
        cols = _transfer(q1, q2, V)
        trans[q1] = cols
        cands.append((_loglik(h, _double_pmf(q1, V, lam, gam, cols)), q1))
    cands.sort(reverse=True)
    best_q1, best_ll = 1, single
    for _, q1 in cands[:3]:
        cols = trans[q1]
        fd = lambda x: -_loglik(h, _double_pmf(q1, V, *_params(x), cols))  # noqa: E731
        x, f = _nelder_mead(fd, best_x, step=(0.4, 0.6), iters=50)
        if -f > best_ll:
            best_q1, best_ll = q1, -f
    return {'q1': best_q1, 'llr': best_ll - single, 'single': single}


def double_compression(coefs, nblocks, luma_table, time_limit=None, freqs=DQ_FREQS):
    """Looks for an earlier save on the same grid in the luma coefficients:
    per AC frequency (the first freqs in zigzag order, of the first 35,
    whose last step is 2 or more; a step of 1 shows nothing), the
    first step that fits best and how much better than a single save (in
    nats per block). A verdict: 'likely' when three or more frequencies
    fit a first step larger than the last one by 0.1 nats per block or
    more (single saves stay below 0.02 in the tests and on the samples),
    'weak' for 0.03 or more, 'none' else, 'unknown' when too few
    frequencies could be looked at."""
    t0 = time.monotonic()
    out = {'blocks': nblocks, 'frequencies': [], 'verdict': 'unknown', 'primary': None}
    if nblocks < 200:
        out['reason'] = 'too few blocks'
        return out
    for k in range(1, 36):
        if sum('score' in f for f in out['frequencies']) >= freqs:
            break
        idx = ZIGZAG[k]
        q2 = luma_table[idx]
        entry = {'zigzag': k, 'index': idx, 'q2': q2}
        if q2 < 2:
            continue
        h = histogram_abs(coefs, nblocks, idx)
        nonzero = sum(h[1:])
        if nonzero < 100:
            entry['skipped'] = 'too few non-zero coefficients (%d)' % nonzero
            out['frequencies'].append(entry)
            continue
        r = analyze_frequency(h, q2)
        entry.update({'q1': r['q1'], 'score': r['llr'] / nblocks, 'nonzero': nonzero})
        out['frequencies'].append(entry)
        if time_limit is not None and time.monotonic() - t0 > time_limit:
            out['stopped'] = 'time limit'
            break
    looked = [f for f in out['frequencies'] if 'score' in f]
    strong = [f for f in looked if f['q1'] > f['q2'] and f['score'] >= DETECT]
    weak = [f for f in looked if f['q1'] > f['q2'] and f['score'] >= WEAK]
    out['looked_at'] = len(looked)
    out['strong'] = len(strong)
    out['seconds'] = time.monotonic() - t0
    if len(looked) < 3:
        out['verdict'] = 'unknown'
        out['reason'] = ('the quantisation is too fine to tell (steps of 1)'
                         if not out['frequencies'] else 'too few frequencies to look at')
    elif len(strong) >= 3:
        out['verdict'] = 'likely'
        out['primary'] = primary_quality(strong)
    elif len(weak) >= 3:
        out['verdict'] = 'weak'
    else:
        out['verdict'] = 'none'
    return out


def _fit_single(h, q2):
    """the envelope (lam, gam) of a single save fitted to h, and its log
    likelihood"""
    V = len(h) - 1
    n = sum(h)
    mean = sum(i * c for i, c in enumerate(h)) / max(n, 1) * q2 + 0.3
    fs = lambda x: -_loglik(h, _single_pmf(q2, V, *_params(x)))  # noqa: E731
    best_x, best_f = None, None
    for g0 in (math.log(0.05), math.log(1.5)):
        x, f = _nelder_mead(fs, [math.log(1.0 / mean), g0])
        if best_f is None or f < best_f:
            best_x, best_f = x, f
    return best_x, -best_f


def _mixture_ll(h, pd, ps, iters=60):
    """alpha by EM, and the log likelihood, of h under alpha pd + (1 - alpha) ps"""
    n = float(sum(h))
    a = 0.5
    for _ in range(iters):
        t = 0.0
        for c, d, e in zip(h, pd, ps):
            if c:
                t += c * a * d / (a * d + (1 - a) * e)
        na = t / n
        if abs(na - a) < 1e-6:
            a = na
            break
        a = na
    return a, sum(c * math.log(a * d + (1 - a) * e) for c, d, e in zip(h, pd, ps) if c)


def _floor(p):
    k = len(p)
    return [(1 - EPS) * max(v, 0.0) + EPS / k for v in p]


def mixture_frequency(h, q2, max_q1=None):
    """Part of the image saved twice (with a coarser first step q1), the
    rest once: the histogram h as a mixture alpha P_D + (1 - alpha) P_S of
    the two models (after T. Bianchi and A. Piva, "Image Forgery
    Localization via Block-Grained Analysis of JPEG Artifacts", IEEE TIFS 7
    (3), 2012), alpha by EM, for each q1 > q2 with the envelope of the
    single fit; the best three refitted with the envelope free, so that a
    misfit of the envelope does not pass for a small twice saved part.
    {'q1', 'alpha', 'gain' (log likelihood over a single save), 'ps', 'pd'
    (the two pmfs of |v|)}"""
    V = len(h) - 1
    x0, single = _fit_single(h, q2)
    lam, gam = _params(x0)
    ps0 = _floor(_single_pmf(q2, V, lam, gam))
    cands = []
    for q1 in range(q2 + 1, (max_q1 or min(8 * q2, q2 + 40, 64)) + 1):
        cols = _transfer(q1, q2, V)
        pd = _floor(_double_pmf(q1, V, lam, gam, cols))
        a, ll = _mixture_ll(h, pd, ps0, 30)
        cands.append((ll, q1, cols))
    cands.sort(key=lambda c: -c[0])
    best = {'q1': 1, 'alpha': 0.0, 'gain': 0.0, 'ps': ps0, 'pd': ps0}
    for _, q1, cols in cands[:3]:
        def f(x):
            lm, gm = _params(x)
            ps = _floor(_single_pmf(q2, V, lm, gm))
            pd = _floor(_double_pmf(q1, V, lm, gm, cols))
            return -_mixture_ll(h, pd, ps, 30)[1]
        x, fv = _nelder_mead(f, x0, step=(0.3, 0.5), iters=40)
        lm, gm = _params(x)
        ps = _floor(_single_pmf(q2, V, lm, gm))
        pd = _floor(_double_pmf(q1, V, lm, gm, cols))
        a, ll = _mixture_ll(h, pd, ps)
        if ll - single > best['gain']:
            best = {'q1': q1, 'alpha': a, 'gain': ll - single, 'ps': ps, 'pd': pd}
    return best


MAP_GAIN = 25.0   # nats a frequency must gain to count for the map


def double_map(coefs, bw, rows, luma_table, freqs=DQ_FREQS):
    """A map of where the image looks saved twice on its grid (with a
    coarser first step) and where once: per 8 x 8 block, the log
    likelihood ratio of its coefficients under the two models of the
    frequencies whose mixture fits better than a single save by MAP_GAIN
    nats or more, summed over the 3 x 3 blocks around it, as the
    probability that they were saved twice (the prior: the share alpha
    the mixtures found). {'map': bytes, bw x rows, 0 to 255 (255: saved
    twice), 'frequencies': [...], 'alpha'}; map None when no frequency
    counts."""
    nblocks = bw * rows
    used = []
    info = []
    for k in range(1, 36):
        if len(info) >= freqs:
            break
        idx = ZIGZAG[k]
        q2 = luma_table[idx]
        if q2 < 2:
            continue
        h = histogram_abs(coefs, nblocks, idx)
        if sum(h[1:]) < 100:
            continue
        m = mixture_frequency(h, q2)
        entry = {'zigzag': k, 'index': idx, 'q2': q2, 'q1': m['q1'], 'alpha': m['alpha'],
                 'gain': m['gain']}
        info.append(entry)
        if m['q1'] > q2 and m['gain'] >= MAP_GAIN and 0 < m['alpha'] < 1:
            V = len(h) - 1
            used.append((idx, V, [math.log(d / e) for d, e in zip(m['pd'], m['ps'])],
                         m['alpha']))
    out = {'frequencies': info, 'used': len(used), 'map': None, 'alpha': None}
    if not used:
        return out
    llr = [0.0] * nblocks
    for idx, V, ratio, _ in used:
        for b in range(nblocks):
            v = coefs[64 * b + idx]
            if v < 0:
                v = -v
            llr[b] += ratio[v if v < V else V]
    alpha = sum(a for _, _, _, a in used) / len(used)
    alpha = min(max(alpha, 1e-3), 1 - 1e-3)
    prior = math.log(alpha / (1 - alpha))
    pix = bytearray(nblocks)
    for y in range(rows):
        for x in range(bw):
            s = 0.0
            for yy in range(max(y - 1, 0), min(y + 2, rows)):
                row = yy * bw
                for xx in range(max(x - 1, 0), min(x + 2, bw)):
                    s += llr[row + xx]
            z = s + prior
            p = 1.0 / (1.0 + math.exp(-z)) if z > -40 else 0.0
            pix[y * bw + x] = int(p * 255 + 0.5)
    out['map'] = bytes(pix)
    out['alpha'] = alpha
    out['share_double'] = sum(1 for v in pix if v >= 128) / float(nblocks)
    return out


def map_of_file(path, time_limit=120.0):
    """double_map () of a file's whole luma: {'map', 'bw', 'rows', ...} or
    {'error'}"""
    try:
        with open(path, 'rb') as f:
            data = f.read()
        info = parse(data)
        tq = info['frame']['components'][0]['tq']
        luma = None
        for t in info['tables']:
            if t['id'] == tq:
                luma = t['values']
        if luma is None:
            raise JpegError('no quantisation table for luma')
        dec = decode_luma(data, info, max_blocks=10 ** 9, time_limit=time_limit)
    except (OSError, JpegError) as e:
        return {'error': str(e)}
    m = double_map(dec['coefs'], dec['bw'], dec['rows'], luma)
    m['bw'] = dec['bw']
    m['rows'] = dec['rows']
    m['width'] = info['frame']['width']
    m['height'] = info['frame']['height']
    m['decode_seconds'] = dec['seconds']
    return m


def primary_quality(found):
    """the quality whose table (IJG, or another base table) fits the first
    steps found best: {'quality', 'family', 'matches', 'of', 'steps'}"""
    best = None
    for fi, (name, bl, bc) in enumerate(BASE_TABLES):
        for q in range(1, 101):
            t = scaled_table(bl, q)
            err = sum(abs(t[f['index']] - f['q1']) / f['q1'] for f in found)
            hits = sum(t[f['index']] == f['q1'] for f in found)
            key = (err, -hits, fi, -q)
            if best is None or key < best[0]:
                best = (key, q, fi, hits)
    (err, _, _, _), q, fi, hits = best
    return {'quality': q, 'family': BASE_TABLES[fi][0], 'matches': hits, 'of': len(found),
            'mean_relative_error': err / len(found),
            'steps': {str(f['index']): f['q1'] for f in found}}


# ---------------------------------------------------------------- thumbnail

def thumbnail_info(thumb, image_info):
    """what the Exif thumbnail's own JPEG says, and its framing against the
    image's"""
    out = {'size': len(thumb)}
    try:
        t = parse(thumb)
    except JpegError as e:
        out['error'] = str(e)
        return out
    f = t['frame']
    out.update({'width': f['width'], 'height': f['height'], 'subsampling': subsampling_name(f)})
    q = quality_of(t)
    out['quality'] = q['estimate']
    out['quality_exact'] = q['exact']
    out['quality_method'] = q['method']
    sig = match_signatures(t, None)
    out['signature'] = sig['signature']
    out['matches'] = [{'kind': m['kind'], 'make': m['make'], 'model': m['model'],
                       'quality': m['quality'], 'name': m['name']} for m in sig['matches'][:12]]
    iw, ih = image_info['frame']['width'], image_info['frame']['height']
    out['image_aspect'] = iw / ih if ih else None
    out['thumb_aspect'] = f['width'] / f['height'] if f['height'] else None
    return out


def content_box(rgb, w, h, dark=24, share=0.98):
    """the part of an RGB image (bytes, 3 per pixel) inside black bars at
    its edges (a camera pads a thumbnail to 160 x 120): (x0, y0, x1, y1).
    A row or column is a bar if share of its pixels are darker than
    dark (of 255) in every channel."""
    def dark_row(y):
        n = 0
        row = rgb[3 * w * y:3 * w * (y + 1)]
        for i in range(0, len(row), 3):
            if row[i] < dark and row[i + 1] < dark and row[i + 2] < dark:
                n += 1
        return n >= share * w

    def dark_col(x):
        n = 0
        for y in range(h):
            i = 3 * (w * y + x)
            if rgb[i] < dark and rgb[i + 1] < dark and rgb[i + 2] < dark:
                n += 1
        return n >= share * h

    y0, y1, x0, x1 = 0, h, 0, w
    while y0 < h // 3 and dark_row(y0):
        y0 += 1
    while y1 > h - h // 3 and dark_row(y1 - 1):
        y1 -= 1
    while x0 < w // 3 and dark_col(x0):
        x0 += 1
    while x1 > w - w // 3 and dark_col(x1 - 1):
        x1 -= 1
    # an image that is dark along its edges anyway: no bars on one side only
    if (y0 > 0) != (y1 < h):
        y0, y1 = 0, h
    if (x0 > 0) != (x1 < w):
        x0, x1 = 0, w
    return x0, y0, x1, y1


def compare_pixels(a, b, w, h):
    """two RGB images of the same size (bytes, 3 per pixel): the mean
    absolute difference (levels of 255), its 99th percentile, and the share
    of pixels that differ by more than 32 levels in some channel"""
    n = w * h
    if n == 0 or len(a) < 3 * n or len(b) < 3 * n:
        return None
    hist = [0] * 256
    total = 0
    big = 0
    for i in range(0, 3 * n, 3):
        d0 = abs(a[i] - b[i])
        d1 = abs(a[i + 1] - b[i + 1])
        d2 = abs(a[i + 2] - b[i + 2])
        total += d0 + d1 + d2
        m = max(d0, d1, d2)
        hist[m] += 1
        if m > 32:
            big += 1
    seen = 0
    p99 = 255
    for v in range(256):
        seen += hist[v]
        if seen >= 0.99 * n:
            p99 = v
            break
    return {'mean': total / (3.0 * n), 'p99': p99, 'share_over_32': big / float(n)}


# ---------------------------------------------------------------- the report

def analyze(path, decode=True, max_blocks=60000, time_limit=20.0):
    """The report of a file: a dict that json.dumps takes. Never raises for
    a bad file: 'error' says what was wrong."""
    rep = {'version': VERSION, 'file': path, 'is_jpeg': False, 'error': None, 'hints': [],
           'honest': HONEST, 'suggest': {}}
    if not path:
        rep['error'] = 'The image has no file on disk (it was not opened from a file).'
        return rep
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        rep['error'] = 'The file cannot be read: %s' % e
        return rep
    rep['size'] = len(data)
    try:
        info = parse(data)
    except JpegError as e:
        rep['error'] = 'Not a JPEG file that can be read: %s.' % e
        return rep
    rep['is_jpeg'] = True
    fr = info['frame']
    rep['frame'] = {k: fr[k] for k in ('marker', 'kind', 'precision', 'width', 'height',
                                        'components', 'progressive')}
    rep['frame']['subsampling'] = subsampling_name(fr)
    rep['segments'] = info['segments']
    rep['scans'] = len(info['scans'])
    rep['restart_interval'] = info['restart_interval']
    rep['huffman'] = huffman_kind(info)
    rep['trailer'] = info['trailer']
    rep['problems'] = list(info['problems'])
    rep['tables'] = [{'id': t['id'], 'precision': t['precision'], 'values': t['values']}
                     for t in info['tables']]
    rep['quality'] = quality_of(info)
    exif = info['exif']
    meta = {'jfif': info['jfif'], 'xmp': info['xmp'], 'icc': info['icc'],
            'photoshop': info['photoshop'], 'ducky': info['ducky'], 'adobe': info['adobe'],
            'mpf': info['mpf'],
            'c2pa': info['c2pa'], 'comments': info['comments'], 'app': info['app']}
    if exif:
        meta['exif'] = {'camera': exif['ifd0'], 'exif': exif['exif'],
                        'makernote': exif['makernote'], 'gps': exif['gps'],
                        'thumbnail': bool(exif['thumbnail']), 'problems': exif['problems']}
    else:
        meta['exif'] = None
    rep['metadata'] = meta
    sig = match_signatures(info, exif)
    rep['signatures'] = {'signature': sig['signature'], 'rotated': sig['rotated'],
                         'subsampling': sig.get('subsampling'),
                         'matches': [{k: m[k] for k in ('kind', 'make', 'model', 'quality',
                                                        'name', 'subsampling',
                                                        'subsampling_matches',
                                                        'make_model_matches',
                                                        'software_matches')}
                                     for m in sig['matches']]}
    rep['assessment'] = assessment(info, sig)
    if exif and exif['thumbnail']:
        rep['thumbnail'] = thumbnail_info(exif['thumbnail'], info)
    else:
        rep['thumbnail'] = None

    # double compression
    rep['double'] = None
    if decode:
        luma_tq = fr['components'][0]['tq']
        luma = None
        for t in info['tables']:
            if t['id'] == luma_tq:
                luma = t['values']
        try:
            if luma is None:
                raise JpegError('no quantisation table for luma')
            t0 = time.monotonic()
            dec = decode_luma(data, info, max_blocks=max_blocks, time_limit=time_limit)
            left = None if time_limit is None else max(time_limit - (time.monotonic() - t0), 2.0)
            dq = double_compression(dec['coefs'], dec['blocks'], luma, time_limit=left)
            dq['decoded_rows'] = dec['rows']
            dq['total_rows'] = dec['total_rows']
            dq['decode_seconds'] = dec['seconds']
            rep['double'] = dq
        except JpegError as e:
            rep['double'] = {'verdict': 'unknown', 'reason': str(e), 'frequencies': []}
    make_hints(rep)
    return rep


def make_hints(rep):
    """short findings, each 'edit' (a sign of processing), 'note' or 'ok'"""
    hints = rep['hints']
    q = rep['quality']
    meta = rep['metadata']
    exif = meta.get('exif') or {}
    cam = exif.get('camera') or {}
    sw = cam.get('Software')
    if q.get('exact'):
        hints.append(('note', 'Saved last with the standard %s at quality %d.' % (
            'IJG (libjpeg) tables' if q.get('family_index') == 0 else 'tables of ' + q['family'],
            q['estimate'])))
    elif q.get('estimate'):
        hints.append(('note', 'Custom quantisation tables (a camera, Photoshop or another '
                              'encoder); about quality %d on the IJG scale.' % q['estimate']))
    sigs = rep['signatures']['matches']
    cams = [m for m in sigs if m['kind'] == 'cam' and m['subsampling_matches']]
    sws = [m for m in sigs if m['kind'] == 'sw']
    if cams:
        same = [m for m in cams if m['make_model_matches']]
        if same:
            hints.append(('ok', 'The tables are those of the camera the Exif data name (%s %s, '
                                '"%s").' % (same[0]['make'], same[0]['model'],
                                            same[0]['quality'])))
        else:
            names = sorted({_camera_name(m) for m in cams})
            hints.append(('note', 'The tables match %d camera%s in the database (%s)%s.' % (
                len(names), '' if len(names) == 1 else 's', ', '.join(names[:4]) +
                (', ...' if len(names) > 4 else ''),
                ', not the one the Exif data name (%s %s)' % (cam.get('Make'), cam.get('Model'))
                if cam.get('Make') else '')))
    if sws:
        names = sorted({'%s (%s)' % (m['name'], m['quality']) if (m.get('quality') or '').strip()
                        else m['name'] for m in sws})
        hints.append(('note', 'The tables match software in the database: %s%s.' % (
            ', '.join(names[:5]), ' and more' if len(names) > 5 else '')))
    if cam.get('Make') and not cams and not q.get('exact'):
        hints.append(('note', 'No signature of %s %s is in the database (it knows cameras up '
                              'to about 2018).' % (cam.get('Make'), cam.get('Model'))))
    if sw:
        hints.append(('note', 'Exif Software: "%s".' % sw))
    xmp = meta.get('xmp') or {}
    if xmp.get('CreatorTool'):
        hints.append(('note', 'XMP CreatorTool: %s.' % ', '.join(xmp['CreatorTool'])))
    if xmp.get('History'):
        hints.append(('edit', 'XMP has an editing history (xmpMM:History).'))
    ps = meta.get('photoshop')
    if ps:
        if ps.get('quality'):
            hints.append(('edit', 'Photoshop resources (APP13): saved with Photoshop\'s Save As '
                                  'at quality %d of 12.' % ps['quality']))
        else:
            hints.append(('note', 'Photoshop resources (APP13), without a quality setting '
                                  '(several programs write them).'))
    if meta.get('ducky') is not None:
        hints.append(('edit', 'Saved with Photoshop\'s Save for Web at quality %d.' % meta['ducky']))
    for c in meta.get('comments') or []:
        if c:
            hints.append(('note', 'Comment: "%s".' % c[:80]))
    if exif.get('camera') is not None and not exif.get('makernote') and cam.get('Make'):
        hints.append(('note', 'No maker notes in Exif: cameras write them, many editors drop '
                              'them.'))
    if rep.get('trailer', 0) > 16:
        hints.append(('note', '%d bytes after the end of the image (another image, or data '
                              'a program appended).' % rep['trailer']))
    if meta.get('c2pa'):
        hints.append(('note', 'The file has Content Credentials (C2PA): see Image > Forensics '
                              '> Content Credentials.'))
    t = rep.get('thumbnail')
    if t and t.get('width'):
        ia, ta = t.get('image_aspect'), t.get('thumb_aspect')
        hints.append(('note', 'An Exif thumbnail of %d x %d%s.' % (
            t['width'], t['height'],
            ', saved at IJG quality %d' % t['quality'] if t.get('quality_exact') else '')))
        if ia and ta and abs(ia / ta - 1) > 0.02:
            hints.append(('note', 'The thumbnail\'s shape (%.3f) differs from the image\'s '
                                  '(%.3f): black bars, a camera that squeezes its thumbnails, '
                                  'or the image was cropped or resized after the thumbnail '
                                  'was made.'
                                  % (ta, ia)))
    d = rep.get('double')
    if d:
        if d['verdict'] == 'likely':
            p = d.get('primary') or {}
            hints.append(('edit', 'Double compression: the coefficients show an earlier save '
                                  'on the same grid, with larger steps%s. The image was '
                                  'saved as JPEG at least twice.' % (
                                      ', about quality %d (%s)' % (p['quality'], p['family'])
                                      if p else '')))
        elif d['verdict'] == 'weak':
            hints.append(('note', 'A weak hint of double compression in a few frequencies: '
                                  'not conclusive.'))
        elif d['verdict'] == 'none':
            hints.append(('ok', 'No sign of an earlier save with larger steps on the same grid '
                                '(an earlier save at a higher quality, or with another grid, '
                                'would not show).'))
    # suggestions for the Workbench
    if q.get('estimate'):
        rep['suggest']['ela_quality'] = int(q['estimate'])
    if d and d.get('verdict') == 'likely' and d.get('primary'):
        rep['suggest']['ghost_quality'] = int(d['primary']['quality'])


def to_text(rep):
    """the report as plain text (for the clipboard)"""
    L = []
    L.append('JPEG Info (gimp-forensics %s)' % rep.get('version'))
    L.append('File: %s' % rep.get('file'))
    if rep.get('error'):
        L.append(rep['error'])
        return '\n'.join(L) + '\n'
    fr = rep['frame']
    L.append('Size: %d x %d, %s, %s, %d bit, %d bytes' % (
        fr['width'], fr['height'], fr['kind'], fr['subsampling'], fr['precision'], rep['size']))
    q = rep['quality']
    L.append('Quality: %s (%s)' % (q.get('estimate'), q.get('method')))
    for t in rep['tables']:
        L.append('Quantisation table %d (%d bit):' % (t['id'], t['precision']))
        for r in range(8):
            L.append('   ' + ' '.join('%4d' % v for v in t['values'][8 * r:8 * r + 8]))
    L.append('Huffman tables: %s; scans: %d; restart interval: %d' % (
        rep['huffman'], rep['scans'], rep['restart_interval']))
    a = rep['assessment']
    L.append('JPEGsnoop assessment: class %d, %s' % (a['class'], a['text']))
    for r in a['reasons']:
        L.append('   - %s' % r)
    s = rep['signatures']
    L.append('Signature: %s (rotated %s), %d match(es)' % (s['signature'], s['rotated'],
                                                           len(s['matches'])))
    for m in s['matches'][:40]:
        L.append('   %s: %s %s [%s]%s' % (m['kind'], m['make'] or m['name'], m['model'],
                                          m['quality'],
                                          ' (make and model match)' if m['make_model_matches']
                                          else ''))
    d = rep.get('double')
    if d:
        L.append('Double compression: %s%s' % (d['verdict'], (' (%s)' % d['reason'])
                                              if d.get('reason') else ''))
        for f in d.get('frequencies', []):
            if 'score' in f:
                L.append('   zigzag %2d: last step %2d, best first step %2d, %.3f nats per block'
                         % (f['zigzag'], f['q2'], f['q1'], f['score']))
        if d.get('primary'):
            p = d['primary']
            L.append('   first save: about quality %d (%s), %d of %d steps exact' % (
                p['quality'], p['family'], p['matches'], p['of']))
    t = rep.get('thumbnail')
    if t:
        L.append('Exif thumbnail: %s x %s, %d bytes, quality %s' % (
            t.get('width'), t.get('height'), t['size'], t.get('quality')))
        if t.get('compare'):
            c = t['compare']
            L.append('   against the image: mean difference %.1f levels, %.1f %% of the '
                     'pixels over 32' % (c['mean'], 100 * c['share_over_32']))
    L.append('Findings:')
    for level, text in rep['hints']:
        L.append('   [%s] %s' % (level, text))
    L.append('')
    L.extend(rep['honest'])
    return '\n'.join(L) + '\n'
