#!/usr/bin/env python3
# Downloads the C2PA trust lists into trust/ (they are kept in the
# repository, so that the plug-in checks trust offline) and writes
# trust/SOURCES.txt with where each came from and its SHA-256.
#
#   plug-ins/content-credentials/update-trust-lists.py            the pinned commits
#   plug-ins/content-credentials/update-trust-lists.py --latest   the newest ones
#
# The lists:
# - C2PA Trust List and C2PA TSA Trust List, the official lists of the
#   C2PA Conformance Program (github.com/c2pa-org/conformance-public,
#   CC BY 4.0, Coalition for Content Provenance and Authenticity)
# - the Interim Trust List (ITL) that contentcredentials.org/verify used
#   from 2021 to 2025, frozen since 2026-01-01; still what older signed
#   files (Adobe, OpenAI, cameras of before the Conformance Program) chain
#   to (github.com/contentauth/verify-site, Apache-2.0, Adobe)
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TRUST = os.path.join(HERE, 'trust')
PINNED = {
    'c2pa-org/conformance-public': '99927caef670ca4ad9da5e5542dca39e42fad6f3',
    'contentauth/verify-site': '83b2e30684086a62e132f5d4c148abd0151e9486',
}
FILES = [
    ('c2pa-org/conformance-public', 'trust-list/C2PA-TRUST-LIST.pem', 'C2PA-TRUST-LIST.pem',
     'CC-BY-4.0, Coalition for Content Provenance and Authenticity (C2PA)'),
    ('c2pa-org/conformance-public', 'trust-list/C2PA-TSA-TRUST-LIST.pem',
     'C2PA-TSA-TRUST-LIST.pem',
     'CC-BY-4.0, Coalition for Content Provenance and Authenticity (C2PA)'),
    ('contentauth/verify-site', 'static/trust/anchors.pem', 'itl-anchors.pem',
     'Apache-2.0, Adobe (Content Authenticity Initiative)'),
    ('contentauth/verify-site', 'static/trust/allowed.pem', 'itl-allowed.pem',
     'Apache-2.0, Adobe (Content Authenticity Initiative)'),
    ('contentauth/verify-site', 'static/trust/store.cfg', 'itl-store.cfg',
     'Apache-2.0, Adobe (Content Authenticity Initiative)'),
]


def latest(repo):
    url = 'https://api.github.com/repos/%s/commits/main' % repo
    with urllib.request.urlopen(url, timeout=60) as r:
        return json.load(r)['sha']


def main():
    commits = dict(PINNED)
    if '--latest' in sys.argv[1:]:
        commits = {repo: latest(repo) for repo in commits}
    os.makedirs(TRUST, exist_ok=True)
    lines = ['Trust lists used by the Content Credentials plug-in, offline.',
             'Written by update-trust-lists.py; do not edit by hand.',
             'CC-BY-4.0: https://creativecommons.org/licenses/by/4.0/ (the C2PA lists, '
             'unchanged).',
             'Apache-2.0: LICENSE-Apache-2.0.txt next to this file (the Interim Trust List '
             'files, unchanged).', '']
    for repo, path, name, licence in FILES:
        url = 'https://raw.githubusercontent.com/%s/%s/%s' % (repo, commits[repo], path)
        print('fetching', url, file=sys.stderr)
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        with open(os.path.join(TRUST, name), 'wb') as f:
            f.write(data)
        lines += ['%s' % name,
                  '  from https://github.com/%s/blob/%s/%s' % (repo, commits[repo], path),
                  '  licence %s' % licence,
                  '  sha256 %s' % hashlib.sha256(data).hexdigest(),
                  '  certificates %d' % data.count(b'-----BEGIN CERTIFICATE-----'), '']
    with open(os.path.join(TRUST, 'SOURCES.txt'), 'w') as f:
        f.write('\n'.join(lines))
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
