#!/usr/bin/env python3
# Checks of jpeg_report.py (the JPEG Info plug-in's report) without GIMP,
# in the Python of the GIMP Flatpak (tests/jpeg-info/run.sh), on the
# fixtures of make-fixtures.py and, if they are there, the sample images
# (samples/images):
#
# - the signature algorithm against JPEGsnoop's 247 IJG entries;
# - libjpeg's tables and quality scaling, the exact quality of GIMP's
#   exports (baseline, progressive, 4:4:4, 4:2:2, gray), Sherloq's
#   estimate for tables that are not standard;
# - the coefficient decoder against libjpeg (jpeg-coefficients, given as
#   JI_COEFS): the same numbers for every fixture and sample;
# - double compression: found with the right first quality where the
#   first save was coarser; not found for single saves, a finer first
#   save, or grids that do not line up;
# - Exif, the thumbnail, its black bars, the pixel comparison;
# - damaged, cut off and non-JPEG files give an error, not an exception.
#
# Prints PASS or FAIL per check and "JI unit failures: N".
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import glob
import json
import os
import re
import subprocess
import sys
import time

SRC = os.environ['JI_SRC']
OUT = os.environ['JI_OUT']
COEFS = os.environ.get('JI_COEFS')
sys.path.insert(0, os.path.join(SRC, 'plug-ins', 'jpeg-info'))
import jpeg_report as jr  # noqa: E402

FIX = os.path.join(OUT, 'fixtures')
failures = 0


def check(name, ok, msg=''):
    global failures
    print('%s  ji_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failures += 1


def fx(name):
    return os.path.join(FIX, name)


# ---- signatures: JPEGsnoop's IJG entries are reproduced
db = jr.load_signatures()
ijg = [r for r in db if r['name'] == 'IJG Library' and r['sig'] != 'zsig']
bad = []
for r in ijg:
    q = int(r['quality'][:3])
    base = 'Baseline' in r['quality']
    lu, ch = jr.ijg_tables(q, base)
    tables = [lu, None if 'Gray' in r['quality'] else ch, None, None]
    if {jr.jpegsnoop_signature(tables), jr.jpegsnoop_signature(tables, True)} != \
            {r['sig'], r['sigrot']}:
        bad.append(r['quality'])
check('signatures_of_jpegsnoop_ijg_entries', len(ijg) == 247 and not bad,
      '%d entries, %d differ %s' % (len(ijg), len(bad), bad[:5]))
check('signature_database_loaded', len(db) >= 3300, '%d rows' % len(db))

# ---- libjpeg's scaling: quality 50 is the Annex K table, 100 all ones,
# quality 25 twice the table (5000 / 25 = 200 %)
check('ijg_scaling', jr.scaled_table(jr.ANNEX_K_LUMA, 50) == list(jr.ANNEX_K_LUMA) and
      jr.scaled_table(jr.ANNEX_K_LUMA, 100) == [1] * 64 and
      jr.scaled_table(jr.ANNEX_K_LUMA, 25) == [min(2 * v, 255) for v in jr.ANNEX_K_LUMA] and
      jr.scaled_table(jr.ANNEX_K_LUMA, 1)[0] == 255 and
      jr.scaled_table(jr.ANNEX_K_LUMA, 1, False)[0] == 800, None)

# ---- the fixtures: structure and quality
expect = {'q90.jpg': (90, 'baseline', '4:2:0', 'standard'),
          'q75-progressive.jpg': (75, 'progressive', '4:2:0', 'optimized'),
          'q95-444-restart.jpg': (95, 'baseline', '4:4:4', 'standard'),
          'q50-422.jpg': (50, 'baseline', '4:2:2', 'standard'),
          'gray-q80.jpg': (80, 'baseline', 'grayscale', 'standard'),
          'double-70-90.jpg': (90, 'baseline', '4:2:0', 'standard')}
reports = {}
for name in sorted(os.listdir(FIX)):
    t0 = time.monotonic()
    reports[name] = jr.analyze(fx(name))
    reports[name]['_seconds'] = time.monotonic() - t0
for name, (q, kind, sub, huff) in expect.items():
    r = reports[name]
    got = (r['quality']['estimate'], r['frame']['kind'], r['frame']['subsampling'], r['huffman'])
    check('quality_%s' % name.split('.')[0], r['quality']['exact'] and got == (q, kind, sub, huff) and
          r['quality']['family_index'] == 0,
          'quality %s (exact %s), %s, %s, Huffman %s' % (got[0], r['quality']['exact'],
                                                          got[1], got[2], got[3]))
r = reports['q95-444-restart.jpg']
check('restart_interval', r['restart_interval'] == 2 * 80 and
      reports['q85-progressive-restart.jpg']['restart_interval'] == 7,
      '%d and %d MCUs' % (r['restart_interval'],
                          reports['q85-progressive-restart.jpg']['restart_interval']))
check('json', all(json.loads(json.dumps(r)) for r in reports.values()), None)

# Sherloq's estimate: tables of quality 83 with a few entries changed
lu, ch = jr.ijg_tables(83)
lu2 = list(lu)
lu2[10] += 3
lu2[40] -= 2
ch2 = list(ch)
ch2[5] += 4
q, d = jr.sherloq_estimate(lu2, ch2)
check('sherloq_estimate', abs(q - 83) <= 1 and 0 < d < 0.5 and jr.exact_family_match(lu2, ch2) is None,
      'estimate %d, deviation %.3f' % (q, d))
# mozjpeg's default (the ImageMagick table) at quality 75
name, bl, bc = jr.BASE_TABLES[3]
m = jr.exact_family_match(jr.scaled_table(bl, 75), jr.scaled_table(bl, 75))
check('mozjpeg_table_found', m is not None and m[:2] == (3, 75), str(m))

# ---- the decoder against libjpeg
files = [fx(n) for n in sorted(os.listdir(FIX)) if n.endswith('.jpg')]
samples = sorted(glob.glob(os.path.join(SRC, 'samples', 'images', '*.jpg')))
if COEFS and os.access(COEFS, os.X_OK):
    same, differ, secs, blocks = 0, [], 0.0, 0
    for path in files + samples:
        data = open(path, 'rb').read()
        info = jr.parse(data)
        dec = jr.decode_luma(data, info, max_blocks=60000)
        secs += dec['seconds']
        blocks += dec['blocks']
        out = subprocess.run([COEFS, path, str(dec['rows'])], capture_output=True, text=True,
                             check=True).stdout
        ref = [int(v) for line in out.splitlines()
               if line and not line.startswith(('components', 'blocks')) for v in line.split()]
        if list(dec['coefs']) == ref:
            same += 1
        else:
            differ.append(os.path.basename(path))
    kinds = sorted({jr.parse(open(p, 'rb').read())['frame']['kind'] for p in files + samples})
    check('decoder_same_as_libjpeg', not differ,
          '%d files (%s; restart markers; gray; 4:4:4, 4:2:2, 4:2:0), %d the same, %d blocks '
          'in %.1f s; differ: %s' % (len(files + samples), ', '.join(kinds), same, blocks, secs,
                                     differ))
else:
    check('decoder_same_as_libjpeg', False, 'no jpeg-coefficients (JI_COEFS=%s)' % COEFS)

# ---- double compression
d = reports['double-70-90.jpg']['double']
check('double_70_then_90_found', d['verdict'] == 'likely' and d['primary']['quality'] == 70,
      '%s, first save about quality %s, %d of %d frequencies' % (
          d['verdict'], (d.get('primary') or {}).get('quality'), d.get('strong', 0),
          d.get('looked_at', 0)))
d = reports['double-50-85.jpg']['double']
check('double_50_then_85_found', d['verdict'] == 'likely' and
      abs(d['primary']['quality'] - 50) <= 1,
      '%s, first save about quality %s' % (d['verdict'], (d.get('primary') or {}).get('quality')))
single = [n for n in ('q90.jpg', 'q75-progressive.jpg', 'q95-444-restart.jpg', 'q50-422.jpg',
                      'gray-q80.jpg', 'camera-thumb.jpg')]
worst = max(max((f['score'] for f in reports[n]['double']['frequencies'] if 'score' in f),
                default=0) for n in single)
check('single_saves_not_double', all(reports[n]['double']['verdict'] == 'none' for n in single),
      '%s; highest score %.4f nats per block (threshold %.2f)' % (
          ', '.join('%s %s' % (n, reports[n]['double']['verdict']) for n in single), worst,
          jr.DETECT))
for n, why in (('double-90-75.jpg', 'first save finer than the last'),
               ('double-shifted.jpg', 'grid moved by 3 pixels')):
    d = reports[n]['double']
    check('not_seen_%s' % n.split('.')[0].replace('-', '_'), d['verdict'] in ('none', 'weak'),
          '%s: %s (the known limit)' % (why, d['verdict']))

# ---- the double JPEG map: the pasted region saved once, the rest twice
m = jr.map_of_file(fx('splice-into-double.jpg'))
if m.get('map'):
    bw = m['bw']
    inside = [m['map'][y * bw + x] for y in range(20 + 2, 40 - 2) for x in range(40 + 2, 64 - 2)]
    outside = [m['map'][y * bw + x] for y in range(m['rows']) for x in range(bw)
               if not (18 <= y < 42 and 38 <= x < 66)]
    mi, mo = sum(inside) / len(inside), sum(outside) / len(outside)
    check('double_map_splice', mi < 40 and mo > 200,
          'mean %.0f in the pasted region, %.0f elsewhere (255: saved twice), %d frequencies, '
          'first steps %s' % (mi, mo, m['used'], sorted({f['q1'] for f in m['frequencies']})))
else:
    check('double_map_splice', False, str(m.get('error') or m.get('frequencies')))
m = jr.map_of_file(fx('q90.jpg'))
share = m.get('share_double') or 0.0
check('double_map_single_save', m.get('map') is None or share < 0.01,
      'no map' if m.get('map') is None else '%.2f %% of the blocks look saved twice' % (100 * share))

# ---- Exif and thumbnails
r = reports['camera-thumb.jpg']
cam = r['metadata']['exif']['camera']
t = r['thumbnail'] or {}
check('exif_camera', cam.get('Make') == 'NIKON' and cam.get('Model') == 'COOLPIX L23' and
      r['metadata']['exif']['makernote'] == 70, str(cam))
check('thumbnail_found', t.get('width') == 160 and t.get('height') == 120 and
      t.get('quality') == 80 and t.get('quality_exact'), str({k: t.get(k) for k in (
          'width', 'height', 'quality')}))
check('gimp_software_in_exif', reports['q90.jpg']['metadata']['exif']['camera'].get('Software',
                                                                                   '').startswith('GIMP'))
a = reports['q90.jpg']['assessment']
check('assessment_gimp_file', a['class'] == 1, '%d %s' % (a['class'], a['reasons']))
a = r['assessment']
check('assessment_camera_like_file', a['class'] == 4,
      'class %d (the database has no COOLPIX L23 signature at IJG quality 90): %s' % (
          a['class'], a['reasons']))
rb = reports['thumb-bars.jpg']['thumbnail']
check('thumbnail_bars_shape', abs(rb['thumb_aspect'] - 4 / 3) < 0.01 and
      abs(rb['image_aspect'] - 1.5) < 0.01, '%.3f %.3f' % (rb['thumb_aspect'], rb['image_aspect']))

# content_box and compare_pixels on made-up pixels
w, h = 40, 30
img = bytearray(3 * w * h)
for y in range(4, 26):
    for x in range(w):
        img[3 * (y * w + x):3 * (y * w + x) + 3] = bytes([120, 150 + x, 90])
check('content_box_bars', jr.content_box(bytes(img), w, h) == (0, 4, 40, 26),
      str(jr.content_box(bytes(img), w, h)))
full = bytes([200, 30, 30]) * (w * h)
check('content_box_none', jr.content_box(full, w, h) == (0, 0, w, h), None)
other = bytearray(full)
for i in range(0, 3 * 100, 3):
    other[i] = 100
c = jr.compare_pixels(full, bytes(other), w, h)
check('compare_pixels', abs(c['mean'] - 100 * 100 / (3 * w * h)) < 1e-9 and
      abs(c['share_over_32'] - 100 / (w * h)) < 1e-12 and
      jr.compare_pixels(full, full, w, h)['mean'] == 0, str(c))

# ---- bad files: an error, never an exception
good = open(fx('q90.jpg'), 'rb').read()
cases = {'empty': b'', 'png': open(fx('not-jpeg.png'), 'rb').read()[:5000],
         'cut_in_header': good[:300], 'cut_in_scan': good[:len(good) // 2],
         'garbage_scan': good[:1500] + bytes((i * 37) & 255 for i in range(20000)),
         'bad_dqt': good[:22] + b'\xff\xdb\x00\x05\x35' + good[22:]}
ok = True
msgs = []
for label, data in cases.items():
    p = os.path.join(OUT, 'bad-%s.jpg' % label)
    with open(p, 'wb') as f:
        f.write(data)
    try:
        r = jr.analyze(p)
        json.dumps(r)
        msgs.append('%s: %s' % (label, r.get('error') or (r.get('double') or {}).get('reason') or
                                'read'))
    except Exception as e:  # noqa: BLE001 (that is the failure looked for)
        ok = False
        msgs.append('%s: %s %s' % (label, type(e).__name__, e))
check('bad_files_no_exception', ok, '; '.join(msgs))
r = jr.analyze(fx('not-jpeg.png'))
check('png_is_not_jpeg', not r['is_jpeg'] and 'Not a JPEG' in (r['error'] or ''), r['error'])
r = jr.analyze(None)
check('no_file', not r['is_jpeg'] and r['error'], r['error'])

# ---- the text report, and no dashes of the wrong kind anywhere in it
txt = jr.to_text(reports['camera-thumb.jpg'])
check('text_report', 'Quality: 90' in txt and 'JPEGsnoop assessment' in txt and
      not re.search('[\\u2013\\u2014]', txt), '%d lines' % txt.count('\n'))
slow = max(reports.values(), key=lambda r: r['_seconds'])
check('fast_enough', slow['_seconds'] < 10, 'slowest fixture %.1f s' % slow['_seconds'])

# ---- the samples, if they are there: the reports of all of them
if samples:
    lines = []
    for p in samples:
        t0 = time.monotonic()
        r = jr.analyze(p)
        d = r['double'] or {}
        lines.append('%s: quality %s%s, %s, JPEGsnoop class %s, %d signature matches, '
                     'double %s%s, %.1f s' % (
                         os.path.basename(p), r['quality']['estimate'],
                         ' (exact)' if r['quality']['exact'] else '', r['frame']['subsampling'],
                         r['assessment']['class'], len(r['signatures']['matches']),
                         d.get('verdict'), ' (first save about %s)' % d['primary']['quality']
                         if d.get('primary') else '', time.monotonic() - t0))
    with open(os.path.join(OUT, 'samples.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    sd = [line for line in lines if 'double likely' in line]
    check('samples_double', len(sd) == 1 and sd[0].startswith('lake-double-jpeg.jpg') and
          'about 70' in sd[0], '; '.join(sd) or 'none found')

print('JI unit failures: %d' % failures)
sys.exit(1 if failures else 0)
