#!/usr/bin/env python3
# Downloads the test files listed in images.json into images/ (next to
# this script; not in git), checked against their SHA-256. Files already
# there with the right checksum are kept. They are C2PA test files: from
# the C2PA public test files (CC BY-SA 4.0) and from the test fixtures of
# c2pa-rs (MIT OR Apache-2.0); images.json says which is which. No
# personal photos.
#
#   tests/content-credentials/fetch-images.py           fetch what is missing
#   tests/content-credentials/fetch-images.py --check   exit 1 if any is missing
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(HERE, 'images')


def ok(path, sha256):
    if not os.path.isfile(path):
        return False
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest() == sha256


def main():
    with open(os.path.join(HERE, 'images.json')) as f:
        files = json.load(f)['files']
    missing = [e for e in files if not ok(os.path.join(IMAGES, e['name']), e['sha256'])]
    if '--check' in sys.argv[1:]:
        for e in missing:
            print('missing', e['name'])
        sys.exit(1 if missing else 0)
    os.makedirs(IMAGES, exist_ok=True)
    for e in missing:
        print('fetching', e['url'], file=sys.stderr)
        with urllib.request.urlopen(e['url'], timeout=120) as r:
            data = r.read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != e['sha256']:
            sys.exit('fetch-images.py: %s has SHA-256 %s, expected %s' % (e['url'], digest,
                                                                        e['sha256']))
        with open(os.path.join(IMAGES, e['name']), 'wb') as f:
            f.write(data)
    with open(os.path.join(IMAGES, 'CREDITS.txt'), 'w') as f:
        f.write('Test files, downloaded by fetch-images.py (see images.json):\n\n')
        for e in files:
            f.write('%s\n  %s\n  %s; licence %s\n\n' % (e['name'], e['url'], e['by'],
                                                     e['licence']))
    print('%d test files in %s (%d fetched)' % (len(files), IMAGES, len(missing)))


if __name__ == '__main__':
    main()
