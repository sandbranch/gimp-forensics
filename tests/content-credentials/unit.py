#!/usr/bin/env python3
# Checks c2pa_report.py without GIMP: the report on every test file
# (expected.py), where the manifest store is found without the library,
# the library missing, a planted libc2pa_c.so in the current folder, the
# settings (offline), trust with your own anchors, the XMP readers, the
# vocabulary, and the text (plain words, no dashes). tests/run.sh runs it
# with the Python of the GIMP Flatpak; it also runs with any Python 3.
#
# Prints "CC PASS <check>" or "CC FAIL <check>: <why>", and
# "CC unit failures: <n>" at the end.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
PLUGIN = os.path.join(SRC, 'plug-ins', 'content-credentials')
IMAGES = os.path.join(HERE, 'images')
sys.path.insert(0, PLUGIN)
sys.path.insert(0, HERE)
import c2pa_report as R  # noqa: E402
from expected import EXPECTED, check_report  # noqa: E402

failures = 0
passes = 0


def result(name, wrong):
    global failures, passes
    if wrong:
        failures += 1
        print('CC FAIL %s: %s' % (name, '; '.join(wrong)), flush=True)
    else:
        passes += 1
        print('CC PASS %s' % name, flush=True)


def check(name, cond, why=''):
    result(name, [] if cond else [why or 'not so'])


# ------------------------------------------------------------ every test file

reports = {}
for name, exp in EXPECTED.items():
    path = os.path.join(IMAGES, name)
    if not os.path.isfile(path):
        result('report ' + name, ['missing: run fetch-images.py and make-fixtures.py'])
        continue
    try:
        r = R.analyze(path)
        json.dumps(r)
    except Exception as e:
        result('report ' + name, ['%s: %s' % (type(e).__name__, e)])
        continue
    reports[name] = r
    result('report ' + name, check_report(name, r, exp))

# the manifest store found without the library agrees with the library
wrong = []
for name, r in reports.items():
    if r['status'] in ('remote',):
        continue
    has_store = r['status'] not in ('none', 'nofile')
    if has_store != bool(r['container']):
        wrong.append('%s: library %s, located %s' % (name, r['status'], r['container']))
result('locate() agrees with the library on %d files' % len(reports), wrong)

# ------------------------------------------------------------ details

r = reports.get('c2pa-rs-ocsp.jpg')
if r:
    a = r['active']
    check('ocsp.jpg: claim generator', a['claim_generator'] == 'Adobe Photoshop 26.9.0',
          a['claim_generator'])
    check('ocsp.jpg: signer', a['signer']['common_name'] == 'Adobe C2PA' and
          a['signer']['issuer'] == 'Adobe Inc.', str(a['signer']))
    check('ocsp.jpg: time of signing', a['signer']['time'].startswith('2025-08-13T16:30:55'),
          a['signer']['time'])
    acts = [(x['action'], x['software_agent']) for x in a['actions']]
    check('ocsp.jpg: actions', acts == [('c2pa.opened', ''), ('c2pa.cropped', ''),
                                        ('c2pa.edited', 'Adobe Firefly')], str(acts))
    ing = a['ingredients'][0]
    check('ocsp.jpg: the ingredient is Firefly\'s, signer trusted',
          ing['trust'] == 'trusted' and ing['manifest']['claim_generator'].startswith(
              'Adobe_Firefly') and ing['manifest']['actions'][0]['digital_source_type']['term']
          == 'trainedAlgorithmicMedia', str(ing))
    check('ocsp.jpg: XMP has no digital source type (only the manifest says it)',
          not [x for x in r['xmp'] if x['kind'] == 'DigitalSourceType'], str(r['xmp']))
    summary = R.ai_summary(r)
    check('ocsp.jpg: the AI line names the IPTC terms', summary and
          'compositeWithTrainedAlgorithmicMedia' in summary and
          'trainedAlgorithmicMedia"' not in summary and 'an ingredient' in summary, summary)
    check('ocsp.jpg: says where each AI finding came from', all(
        x['source'].startswith('C2PA manifest') and 'signed by' in x['source']
        for x in r['ai']), str([x['source'] for x in r['ai']]))

r = reports.get('adobe-20220124-CIE-sig-CA.jpg')
if r:
    ing = r['active']['ingredients'][0]
    check('CIE-sig-CA: the ingredient\'s bad signature is on the ingredient',
          [p['code'] for p in ing['problems']][:2] == ['timeStamp.mismatch',
                                                       'claimSignature.mismatch'],
          str(ing['problems']))
    check('CIE-sig-CA: explained', any('ingredient E-sig-CA.jpg' in e
                                       for e in r['explanation']), str(r['explanation']))

r = reports.get('adobe-20220124-E-clm-CAICAI.jpg')
if r:
    ing = r['active']['ingredients'][1]
    check('E-clm-CAICAI: the missing ingredient manifest is marked', ing['manifest_missing'] and
          R.ingredient_note(ing) == ', its credentials are missing from the file', str(ing))

r = reports.get('c2pa-rs-cloud.jpg')
if r:
    check('cloud.jpg: the remote manifest is not fetched', r['remote_url'] ==
          'https://cai-manifests.adobe.com/manifests/'
          'adobe-urn-uuid-5f37e182-3687-462e-a7fb-573462780391' and
          any('does not go online' in e for e in r['explanation']), str(r['explanation']))

r = reports.get('generated/tampered-pixels.png')
if r:
    check('tampered-pixels.png: explained as changed image data', any(
        'image data was changed' in e for e in r['explanation']), str(r['explanation']))

for name in ('generated/xmp-ai.jpg', 'generated/xmp-ai.png'):
    r = reports.get(name)
    if r:
        check(name + ': the AI finding is from unsigned XMP', r['ai'] and all(
            'XMP metadata in the file' in a['source'] and 'not signed' in a['source']
            for a in r['ai']) and 'not signed' in (R.ai_summary(r) or ''), str(r['ai']))

# GIMP's metadata is used first, and said so
path = os.path.join(IMAGES, 'generated', 'xmp-ai.jpg')
if os.path.isfile(path):
    r = R.analyze(path, gimp_xmp={'DigitalSourceType': [R.IPTC_DST + 'compositeSynthetic'],
                                  'provenance': [], 'CreatorTool': []},
                  gimp_xmp_source='GIMP')
    check('XMP from GIMP comes first', [x['source'] for x in r['xmp']] == ['GIMP'] and
          r['ai'][0]['type']['term'] == 'compositeSynthetic', str(r['xmp']))
    r = R.analyze(path, gimp_xmp={'DigitalSourceType': [], 'provenance': [],
                                  'CreatorTool': []}, gimp_xmp_source='GIMP')
    check('XMP from the file when GIMP has none', [x['source'] for x in r['xmp']] ==
          ['XMP metadata in the file'] * 2, str(r['xmp']))

# your own trust anchors
path = os.path.join(IMAGES, 'generated', 'ai-edited.jpg')
anchors = os.path.join(IMAGES, 'c2pa-rs-test_cert_root_bundle.pem')
if os.path.isfile(path) and os.path.isfile(anchors):
    r = R.analyze(path, user_trust_file=anchors)
    check('your own trust anchors make the test signer trusted', r['status'] == 'trusted' and
          r['trusted_by'] == 'your own trust anchors' and
          r['active']['ingredients'][1]['trust'] == 'trusted', r['status'])
    r = R.analyze(path, user_trust_file=os.path.join(IMAGES, 'no-such.pem'))
    check('a missing trust anchor file is no error', r['status'] == 'untrusted', r['status'])

# a signer is trusted only through a manifest signing list: a CA that is
# only on a time stamp authority (TSA) list does not make its signers
# trusted, and the list named is always the first manifest list, in order,
# that the signer chains to (c2pa-rs itself tries all lists, of any kind,
# in an order that changes from one read to the next)
path = os.path.join(IMAGES, 'generated', 'test-ca-signed.png')
test_ca = os.path.join(IMAGES, 'generated', 'test-ca', 'ca.pem')
if os.path.isfile(path) and os.path.isfile(test_ca):
    real_lists = R.trust_lists
    with open(test_ca) as f:
        test_pem = f.read()

    def extra(name, uri, kind):
        return {'name': name, 'uri': uri, 'kind': kind, 'file': test_ca, 'pem': test_pem,
                'certs': 1}

    def with_lists(before=(), after=()):
        R.trust_lists = lambda user_file=None: (list(before) + real_lists(user_file) +
                                                list(after))

    try:
        tsa = extra('test TSA list', 'test-tsa-list', 'tsa')
        with_lists(after=[tsa])
        runs = [R.analyze(path) for _ in range(8)]
        check('a CA only on a TSA list: the signer is UNKNOWN SIGNER, not trusted',
              all(r['status'] == 'untrusted' and not r.get('trusted_by') and
                  [p['code'] for p in r['problems']] == ['signingCredential.untrusted']
                  for r in runs), str([(r['status'], r.get('trusted_by')) for r in runs]))
        runs = [R.analyze(path, user_trust_file=test_ca) for _ in range(8)]
        check('the same CA in your own anchors too: trusted, by your own anchors every time',
              all(r['status'] == 'trusted' and r['trusted_by'] == 'your own trust anchors'
                  for r in runs), str([(r['status'], r.get('trusted_by')) for r in runs]))
        # on two manifest lists and the TSA list (as Google's root is on the
        # C2PA Trust List and the TSA list): the first manifest list, always
        with_lists(before=[extra('test manifest list', 'test-manifest-list', 'manifest')],
                   after=[tsa])
        runs = [R.analyze(path, user_trust_file=test_ca) for _ in range(8)]
        check('a CA on two manifest lists and the TSA list: the first list named every time',
              all(r['status'] == 'trusted' and r['trusted_by'] == 'test manifest list'
                  for r in runs), str([(r['status'], r.get('trusted_by')) for r in runs]))
        # the time stamp results come from the read with the TSA list
        with_lists(after=[tsa])
        c2pa = R.load_c2pa()
        first, _ = R.read_store(c2pa, path, R.trust_lists(test_ca))
        store, name = R.signer_trust(c2pa, path, R.trust_lists(test_ca), first)
        check('signer_trust: trusted by your own anchors, from a read without the TSA list',
              name == 'your own trust anchors' and R.trusted_uri(store) == 'user-trust-anchors',
              '%s %s' % (name, R.trusted_uri(store)))
    finally:
        R.trust_lists = real_lists
else:
    result('TSA list trust tests', ['missing: run make-fixtures.py'])

s = R.settings(R.trust_lists(), timestamp_trust=False)
check('settings: time stamp trust can be left to the first read',
      s['verify']['verify_timestamp_trust'] is False and s['verify']['verify_trust'] is True,
      json.dumps(s['verify']))

# no file
r = R.analyze(None)
check('no file: status nofile', r['status'] == 'nofile' and r['headline'], r['status'])

# ------------------------------------------------------------ settings, offline

s = R.settings(R.trust_lists())
check('settings: offline', s['verify']['remote_manifest_fetch'] is False and
      s['verify']['ocsp_fetch'] is False and s['core']['allowed_network_hosts'] == [],
      json.dumps(s['verify']))
names = [t['name'] for t in R.trust_lists()]
check('trust lists: C2PA, Interim, TSA', names == [
    'C2PA Trust List', 'Interim Trust List (frozen 2026-01-01)', 'C2PA TSA Trust List'],
    str(names))
check('trust lists have certificates', all(t['certs'] > 10 for t in R.trust_lists()),
      str([t['certs'] for t in R.trust_lists()]))

# ------------------------------------------------------------ the library

PROBE = r'''
import json, os, sys
sys.path.insert(0, sys.argv[1])
import c2pa_report as R
if len(sys.argv) > 3:
    R.VENDOR = sys.argv[3]
r = R.analyze(sys.argv[2])
lib = sys.modules.get('c2pa.c2pa')
print(json.dumps({'status': r['status'], 'container': r['container'],
                  'loaded': getattr(getattr(lib, '_lib', None), '_name', None),
                  'error': R.library_error()}))
'''


def probe(cwd, image, vendor=None, env=None):
    args = [sys.executable, '-c', PROBE, PLUGIN, image] + ([vendor] if vendor else [])
    out = subprocess.run(args, cwd=cwd, capture_output=True, text=True, env=env, timeout=120)
    try:
        return json.loads(out.stdout.strip().splitlines()[-1])
    except (IndexError, ValueError):
        return {'status': 'crashed', 'error': out.stderr[-500:]}


image = os.path.join(IMAGES, 'adobe-20220124-C.jpg')
tmp = tempfile.mkdtemp(prefix='cc-unit-')
try:
    # a library planted in the current folder, in ./libs and named by
    # C2PA_LIBRARY_NAME is not loaded
    os.makedirs(os.path.join(tmp, 'libs'))
    for p in (os.path.join(tmp, 'libc2pa_c.so'), os.path.join(tmp, 'libs', 'libc2pa_c.so'),
              os.path.join(tmp, 'evil.so')):
        with open(p, 'wb') as f:
            f.write(b'not a library')
    env = dict(os.environ, C2PA_LIBRARY_NAME='evil.so')
    p = probe(tmp, image, env=env)
    check('a libc2pa_c.so planted in the current folder is not loaded',
          p['status'] == 'untrusted' and p['loaded'] and os.path.realpath(p['loaded']) ==
          os.path.realpath(os.path.join(R.VENDOR, 'c2pa', 'libs', 'libc2pa_c.so')), str(p))
    # without the library: says so, and still finds the manifest store
    empty = os.path.join(tmp, 'no-vendor')
    os.makedirs(empty)
    p = probe(empty, image, vendor=empty)
    check('without the library: status unavailable, store still located',
          p['status'] == 'unavailable' and p['container'] and
          p['container']['container'].startswith('JPEG APP11'), str(p))
finally:
    shutil.rmtree(tmp, ignore_errors=True)

# ------------------------------------------------------------ vocabulary, words

iptc = ['digitalCapture', 'computationalCapture', 'negativeFilm', 'positiveFilm', 'print',
        'minorHumanEdits', 'humanEdits', 'compositeWithTrainedAlgorithmicMedia',
        'algorithmicallyEnhanced', 'softwareImage', 'digitalArt', 'digitalCreation',
        'dataDrivenMedia', 'trainedAlgorithmicMedia', 'algorithmicMedia', 'screenCapture',
        'virtualRecording', 'composite', 'compositeCapture', 'compositeSynthetic']
check('all 20 IPTC digital source types are known', all(
    R.source_type(R.IPTC_DST + t)['label'] != R.IPTC_DST + t for t in iptc))
ai = sorted(t for t in iptc if R.source_type(R.IPTC_DST + t)['ai'])
check('generative AI terms', ai == ['composite', 'compositeSynthetic',
                                    'compositeWithTrainedAlgorithmicMedia',
                                    'trainedAlgorithmicMedia', 'virtualRecording'], str(ai))
check('algorithmicMedia is not flagged as AI (no training data)',
      R.source_type(R.IPTC_DST + 'algorithmicMedia')['ai'] is None)
check('an unknown source type keeps its URI', R.source_type('http://x/y/foo')['label'] ==
      'http://x/y/foo')

texts = [R.to_text(r) for r in reports.values()]
DASHES = (chr(0x2014), chr(0x2013))
bad = [t for t in texts if any(d in t for d in DASHES)]
check('no em or en dash in any report', not bad)
check('every report says what credentials do and do not prove', all(
    'do not prove that what the image shows is true' in t and 'Missing credentials prove '
    'nothing' in t for t in texts))

# no em or en dash anywhere in the repository's own text files
dashed = []
for root, dirs, files in os.walk(SRC):
    dirs[:] = [d for d in dirs if d not in ('.git', 'vendor', 'images', 'output',
                                            '__pycache__', 'trust')]
    for f in files:
        if f.endswith(('.py', '.sh', '.md', '.json', '.mjs', '.txt')):
            with open(os.path.join(root, f), encoding='utf-8', errors='replace') as fh:
                text = fh.read()
                if any(d in text for d in DASHES):
                    dashed.append(os.path.relpath(os.path.join(root, f), SRC))
check('no em or en dash in the repository', not dashed, str(dashed))

print('CC unit passed: %d' % passes)
print('CC unit failures: %d' % failures)
sys.exit(1 if failures else 0)
