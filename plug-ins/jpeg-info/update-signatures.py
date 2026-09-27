#!/usr/bin/env python3
# Makes jpegsnoop-signatures.tsv, the compression signatures the JPEG Info
# plug-in compares a file's quantisation tables with, from the signature
# database of JPEGsnoop (Calvin Hass, github.com/ImpulseAdventure/JPEGsnoop,
# source/Signatures.inl; GPL-2.0-or-later, which this repository takes
# under GPL-3.0-or-later). Downloads that one file at a fixed commit (the
# only step that needs the network) and writes the table next to this
# script. The table is in git; run this only to update it.
#
#   plug-ins/jpeg-info/update-signatures.py [--commit HASH] [--from FILE]
#
# Each line: kind (cam for a camera, sw for software), make, model,
# quality (as the camera or program calls it), signature, signature of the
# tables rotated by 90 degrees, chroma subsampling ("2x1" and so on, for
# cameras), a text to look for in the EXIF Software field, the name to
# show. A signature is JPEGsnoop's: "01" and the last 30 hex digits of the
# MD5 of "JPEGsnoop*DQT0,016,011,...,*DQT1,...,*END" (every table the file
# defines, in natural order, three digits each); jpeg_report.py computes
# it the same way (checked against the database's 247 IJG entries in
# tests/jpeg-info/unit.py). Rows with the signature "zsig" only name
# software to look for in the Software field.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
import re
import sys
import urllib.request

COMMIT = '9732ee0961f100eb69bbff4a0c47438d5997abee'   # 2018-07-19, v1.8.0
URL = 'https://raw.githubusercontent.com/ImpulseAdventure/JPEGsnoop/%s/source/Signatures.inl'
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'jpegsnoop-signatures.tsv')

ENTRY = re.compile(r'\{\s*ENUM_EDITOR_(CAM|SW|UNSET)\s*,' + r'\s*_T\("([^"]*)"\)\s*,' * 7 +
                   r'\s*_T\("([^"]*)"\)\s*\}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--commit', default=COMMIT)
    ap.add_argument('--from', dest='source', help='a local copy of Signatures.inl')
    args = ap.parse_args()
    if args.source:
        with open(args.source, encoding='latin-1') as f:
            text = f.read()
    else:
        with urllib.request.urlopen(URL % args.commit, timeout=60) as r:
            text = r.read().decode('latin-1')
    # only the signature list (the exception lists after it are kept in
    # jpeg_report.py)
    text = text[text.index('m_sSigList[]'):]
    text = text[:text.index('\n};')]
    rows = []
    for m in ENTRY.finditer(text):
        kind, make, model, qual, sig, sigrot, subsamp, swtrim, swdisp = m.groups()
        if kind == 'UNSET':
            continue
        rows.append(('cam' if kind == 'CAM' else 'sw', make, model, qual, sig, sigrot,
                     subsamp, swtrim, swdisp))
    if len(rows) < 3000:
        sys.exit('update-signatures.py: only %d entries found, not written' % len(rows))
    for r in rows:
        if any('\t' in v or '\n' in v for v in r):
            sys.exit('update-signatures.py: a tab or newline in %r' % (r,))
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Compression signatures of cameras and software, from JPEGsnoop\n')
        f.write('# (Calvin Hass, https://github.com/ImpulseAdventure/JPEGsnoop, '
                'source/Signatures.inl at %s).\n' % args.commit)
        f.write('# Copyright (C) 2017 Calvin Hass; GNU GPL version 2 or (at your option) '
                'any later version.\n')
        f.write('# Made by update-signatures.py; columns: kind, make, model, quality, '
                'signature, rotated, subsampling, software field, name.\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    print('wrote %d signatures to %s' % (len(rows), OUT))


if __name__ == '__main__':
    main()
