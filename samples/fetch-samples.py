#!/usr/bin/env python3
# Downloads the sample images listed in samples.json into samples/images
# (not in git), each checked against its SHA-256, and writes
# images/CREDITS.txt with the source, author and licence of each. Files
# already there with the right checksum are kept. It also runs
# tests/content-credentials/fetch-images.py, whose C2PA test files the
# README uses by reference (they stay in tests/content-credentials/images).
#
#   samples/fetch-samples.py           fetch what is missing
#   samples/fetch-samples.py --check   exit 1 if any is missing
#
# Python 3, standard library only. The files come from Wikimedia Commons
# (upload.wikimedia.org) and Poly Haven (dl.polyhaven.org), one at a time
# with a pause between them and a User-Agent naming this project, as
# Wikimedia's User-Agent policy asks; on "429 Too Many Requests" it waits
# and tries again. Then run samples/make-edits.py for the edited images.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
IMAGES = os.path.join(HERE, 'images')
C2PA_FETCH = os.path.join(SRC, 'tests', 'content-credentials', 'fetch-images.py')


def ok(path, sha256):
    if not os.path.isfile(path):
        return False
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest() == sha256


def download(url, agent):
    for attempt in range(6):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': agent})
            with urllib.request.urlopen(req, timeout=180) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code not in (429, 503) or attempt == 5:
                raise
            wait = int(e.headers.get('Retry-After') or 0) or 20 * (attempt + 1)
            print('  %d from the server, waiting %d s' % (e.code, wait), file=sys.stderr)
            time.sleep(wait)
    raise RuntimeError('unreachable')


def main():
    with open(os.path.join(HERE, 'samples.json'), encoding='utf-8') as f:
        doc = json.load(f)
    files = doc['files']
    missing = [e for e in files if not ok(os.path.join(IMAGES, e['name']), e['sha256'])]
    if '--check' in sys.argv[1:]:
        for e in missing:
            print('missing', e['name'])
        r = subprocess.run([sys.executable, C2PA_FETCH, '--check'])
        sys.exit(1 if missing or r.returncode else 0)
    os.makedirs(IMAGES, exist_ok=True)
    for i, e in enumerate(missing):
        if i:
            time.sleep(2)
        print('fetching', e['name'], 'from', e['url'], file=sys.stderr)
        data = download(e['url'], doc['user_agent'])
        digest = hashlib.sha256(data).hexdigest()
        if digest != e['sha256']:
            sys.exit('fetch-samples.py: %s has SHA-256 %s, expected %s (the file on the '
                     'server changed: check its page, %s)' % (e['url'], digest, e['sha256'],
                                                              e['page']))
        with open(os.path.join(IMAGES, e['name']), 'wb') as f:
            f.write(data)
    with open(os.path.join(IMAGES, 'CREDITS.txt'), 'w', encoding='utf-8') as f:
        f.write('Sample images, downloaded by samples/fetch-samples.py (see samples.json).\n'
                'Licences as stated on each source page (checked 2026-09-27). The edited\n'
                'images made by samples/make-edits.py are derived from the files marked\n'
                '"source" and are under the same terms (all CC0).\n\n')
        for e in files:
            f.write('%s  [%s]\n  %s\n  by %s\n  licence: %s%s\n  source page: %s\n  file: %s\n\n' % (
                e['name'], e['group'], e['what'], e['author'], e['licence'],
                ' (%s)' % e['licence_url'] if e['licence_url'] else '', e['page'], e['url']))
        f.write('Used by reference, in tests/content-credentials/images (fetched by\n'
                'tests/content-credentials/fetch-images.py, credits in its CREDITS.txt):\n')
        for name in doc['c2pa_test_files']:
            f.write('  %s\n' % name)
    print('%d samples in %s (%d fetched)' % (len(files), IMAGES, len(missing)), flush=True)
    r = subprocess.run([sys.executable, C2PA_FETCH])
    if r.returncode:
        sys.exit('fetch-samples.py: %s failed' % C2PA_FETCH)


if __name__ == '__main__':
    main()
