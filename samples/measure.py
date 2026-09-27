#!/usr/bin/env python3
# Runs the forensics operations of build/ on the sample images with the
# gegl command line, in the GIMP Flatpak isolated from your folders
# (tests/isolate.sh: HOME and the XDG folders in tests/output/gimp-home;
# your folders of GIMP and the other apps are listed before and after and
# must be the same), and prints the numbers that samples/README.md quotes:
# the mean of each result inside the edited region against the rest of the
# image and against the same region of the unedited photo.
#
#   samples/measure.py            everything (about a minute)
#   samples/measure.py --keep     keep the results (tests/output/samples/*.npy)
#   samples/measure.py --png      also write a small PNG of each result there
#
# Needs the operations built (README, "Building"), the samples fetched and
# made (fetch-samples.py, make-edits.py), and numpy and Pillow in the
# Python that runs this script. Values are those GIMP shows (R'G'B', 0 to
# 1), averaged over the three channels.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import shlex
import subprocess
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as e:
    sys.exit('measure.py needs numpy and Pillow (%s)' % e)

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
IMAGES = os.path.join(HERE, 'images')
MASKS = os.path.join(IMAGES, 'masks')
OUT = os.path.join(SRC, 'tests', 'output', 'samples')

ELA = 'forensics:error-level quality=90 scale=20'
GHOST60 = 'forensics:jpeg-ghost quality=60'
GHOST70 = 'forensics:jpeg-ghost quality=70'
GHOSTMIN = 'forensics:jpeg-ghost mode=minimum'
NOISE = 'forensics:noise mode=luminance average=32'
GRAD = 'forensics:luminance-gradient'
CLONE = 'forensics:clone-detect mode=mask'
PCA3 = 'forensics:pca component=3 mode=distance'

ELA_ALL = ['lake-original.jpg', 'lake-splice-plane.jpg', 'lake-clone.jpg',
           'lake-airbrush.jpg', 'lake-double-jpeg.jpg', 'eggs-original.jpg',
           'eggs-mirrored-egg.jpg', 'pin-original.jpg', 'pin-concealed.jpg',
           'sogndal-original.jpg', 'sogndal-composite.jpg', 'ptz-montage.jpg',
           'yezhov-original.jpg', 'yezhov-removed.jpg', 'lincoln-calhoun-composite.jpg',
           'kirksville-fake-tornado.png', 'oranges-camera.jpg', 'lake-colorado-camera.jpg',
           'ribnica-lake-camera.jpg', 'polyhaven-rocks-lossless.png',
           'phone-photo-adobe-cai.jpg', 'firefly-landscape.jpg']
CASES = [(f, 'ela', ELA) for f in ELA_ALL] + [
    ('lake-original.jpg', 'ghost60', GHOST60), ('lake-splice-plane.jpg', 'ghost60', GHOST60),
    ('lake-original.jpg', 'ghost70', GHOST70), ('lake-double-jpeg.jpg', 'ghost70', GHOST70),
    ('lake-original.jpg', 'ghostmin', GHOSTMIN), ('lake-splice-plane.jpg', 'ghostmin', GHOSTMIN),
    ('lake-double-jpeg.jpg', 'ghostmin', GHOSTMIN), ('ribnica-lake-camera.jpg', 'ghostmin', GHOSTMIN),
    ('polyhaven-rocks-lossless.png', 'ghostmin', GHOSTMIN),
    ('lake-original.jpg', 'noise', NOISE), ('lake-airbrush.jpg', 'noise', NOISE),
    ('lake-splice-plane.jpg', 'noise', NOISE),
    ('eggs-original.jpg', 'grad', GRAD), ('eggs-mirrored-egg.jpg', 'grad', GRAD),
    ('yezhov-original.jpg', 'noise', NOISE), ('yezhov-removed.jpg', 'noise', NOISE),
    ('sogndal-composite.jpg', 'noise', NOISE), ('pin-concealed.jpg', 'noise', NOISE),
    ('sogndal-composite.jpg', 'cloneov', 'forensics:clone-detect'),
    ('ptz-montage.jpg', 'cloneov', 'forensics:clone-detect'),
    ('lake-original.jpg', 'pca3', PCA3), ('lake-splice-plane.jpg', 'pca3', PCA3),
] + [(f, 'clone', CLONE) for f in [
    'lake-original.jpg', 'lake-clone.jpg', 'lake-airbrush.jpg', 'eggs-mirrored-egg.jpg',
    'sogndal-composite.jpg', 'sogndal-original.jpg', 'ptz-montage.jpg', 'yezhov-removed.jpg',
    'yezhov-original.jpg', 'lincoln-calhoun-composite.jpg', 'kirksville-fake-tornado.png',
    'pin-concealed.jpg', 'oranges-camera.jpg', 'polyhaven-rocks-lossless.png']]


def out_path(name, label):
    return os.path.join(OUT, '%s.%s.npy' % (os.path.splitext(name)[0], label))


def run_gegl():
    mod = os.path.join(OUT, 'modules')
    os.makedirs(mod, exist_ok=True)
    lines = ['set -e', 'src=%s' % shlex.quote(SRC), 'here="$src/tests"',
             'GIMP_RUN_HOME="$here/output/gimp-home"', 'export GIMP_RUN_HOME',
             '. "$here/isolate.sh"',
             'snapshot_take "$here/output/samples/snapshot-before.txt"',
             'cp "$src"/build/*.so %s/' % shlex.quote(mod)]
    cmds = []
    for name, label, op in CASES:
        cmds.append('gegl %s -o %s -- %s 2>>%s/gegl.log || echo FAILED %s %s' % (
            shlex.quote(os.path.join(IMAGES, name)), shlex.quote(out_path(name, label)), op,
            shlex.quote(OUT), name, label))
    lines.append('gimp_run --timeout=1200 --flatpak --filesystem="$src" '
                 '--env=GEGL_PATH=%s:/app/lib/gegl-0.4 -- sh -c %s' % (
                     shlex.quote(mod), shlex.quote('\n'.join(cmds))))
    lines.append('snapshot_check "$here/output/samples/snapshot-before.txt" ""')
    r = subprocess.run(['sh', '-c', '\n'.join(lines)])
    if r.returncode:
        sys.exit('measure.py: the isolated run failed (%d)' % r.returncode)


def encode(v):
    """linear (what gegl:npy-save writes) to R'G'B', what GIMP shows"""
    v = np.clip(v, 0, None)
    return np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(v, 1 / 2.4) - 0.055)


def result(name, label):
    a = np.load(out_path(name, label)).astype(np.float64)
    return encode(a[..., :3])


def gray(name, label):
    return result(name, label).mean(axis=2)


def load_rgb(name):
    return np.asarray(Image.open(os.path.join(IMAGES, name)).convert('RGB'), dtype=np.float64)


def mask(name):
    return np.asarray(Image.open(os.path.join(MASKS, name)), dtype=np.float64) / 255.0 > 0.5


def blocks_above(v, region, pct, b=16):
    """the share of the region's b x b blocks above the pct percentile of the
    other blocks (below it for pct < 50; blocks at least half in the region
    count as its)"""
    h, w = (v.shape[0] // b) * b, (v.shape[1] // b) * b
    bv = v[:h, :w].reshape(h // b, b, w // b, b).mean(axis=(1, 3))
    br = region[:h, :w].reshape(h // b, b, w // b, b).mean(axis=(1, 3)) > 0.5
    if not br.any():
        return float('nan')
    t = np.percentile(bv[~br], pct)
    return float((bv[br] > t).mean() if pct >= 50 else (bv[br] < t).mean())


def ellipse(shape, cx, cy, rx, ry):
    y, x = np.mgrid[0:shape[0], 0:shape[1]]
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def main():
    os.makedirs(OUT, exist_ok=True)
    run_gegl()
    for name, label, op in CASES:
        if not os.path.isfile(out_path(name, label)):
            sys.exit('measure.py: no result for %s %s (see %s/gegl.log)' % (name, label, OUT))
    rep = []

    def say(s):
        print(s)
        rep.append(s)

    say('== Error Level Analysis (quality 90, scale 20): mean over the image, in 8 bit levels of the result')
    for name in ELA_ALL:
        g = gray(name, 'ela')
        say('  %-32s mean %6.2f   99th percentile %6.2f' % (name, g.mean() * 255,
                                                          np.percentile(g, 99) * 255))

    def region_line(tool, name, orig, label, m, pct=99, k=255):
        e = gray(name, label) * k
        line = '  %-8s %-26s region %7.3f  rest %7.3f  (x%.2f)' % (
            tool, name, e[m].mean(), e[~m].mean(), e[m].mean() / e[~m].mean())
        if orig:
            o = gray(orig, label) * k
            line += '  same region unedited %7.3f (x%.2f)' % (o[m].mean(), e[m].mean() / o[m].mean())
        share = blocks_above(e, m, pct)
        if share == share:
            line += '  blocks %s p%d of the rest %.0f %%' % (
                'above' if pct >= 50 else 'below', pct, 100 * share)
        say(line)

    def rect(name, label, x, y, w, h):
        g = gray(name, label)
        m = np.zeros(g.shape, bool)
        m[y:y + h, x:x + w] = True
        return m

    say('== edited regions: mean of the result in the region, in the rest, in the same region unedited'
        ' (ELA and noise in 8 bit levels of the result, ghost and PCA 0 to 1)')
    sp, cl, ab = mask('lake-splice-plane-mask.png'), mask('lake-clone-mask.png'), \
        mask('lake-airbrush-mask.png')
    region_line('ELA', 'lake-splice-plane.jpg', 'lake-original.jpg', 'ela', sp)
    region_line('ELA', 'lake-clone.jpg', 'lake-original.jpg', 'ela', cl)
    region_line('ELA', 'lake-airbrush.jpg', 'lake-original.jpg', 'ela', ab)
    region_line('Ghost60', 'lake-splice-plane.jpg', 'lake-original.jpg', 'ghost60', sp, 1, 1)
    region_line('Noise', 'lake-airbrush.jpg', 'lake-original.jpg', 'noise', ab, 1)
    region_line('Noise', 'lake-splice-plane.jpg', 'lake-original.jpg', 'noise', sp)
    region_line('PCA3', 'lake-splice-plane.jpg', 'lake-original.jpg', 'pca3', sp, 99, 1)
    # the real edits, in rectangles read off the results (approximate)
    pin = np.abs(load_rgb('pin-concealed.jpg') - load_rgb('pin-original.jpg')).max(axis=2) > 40
    say('  (PIN: the pixels that differ from pin-original.jpg by over 40 levels: %.1f %%)'
        % (100 * pin.mean()))
    region_line('ELA', 'pin-concealed.jpg', 'pin-original.jpg', 'ela', pin)
    region_line('Noise', 'pin-concealed.jpg', None, 'noise', pin)
    cloud = rect('sogndal-composite.jpg', 'ela', 780, 680, 1560, 930)
    region_line('ELA', 'sogndal-composite.jpg (cloud)', None, 'ela', cloud)
    region_line('Noise', 'sogndal-composite.jpg (cloud)', None, 'noise', cloud)
    nata = rect('ptz-montage.jpg', 'ela', 1540, 400, 500, 430)
    region_line('ELA', 'ptz-montage.jpg (figure)', None, 'ela', nata)
    gone = rect('yezhov-removed.jpg', 'noise', 760, 330, 440, 570)
    region_line('ELA', 'yezhov-removed.jpg (right)', None, 'ela', gone)
    region_line('Noise', 'yezhov-removed.jpg (right)', None, 'noise', gone)
    was = rect('yezhov-original.jpg', 'noise', 760, 330, 440, 570)
    region_line('Noise', 'yezhov-original.jpg (right)', None, 'noise', was)

    say('== JPEG Ghost, quality of the smallest difference (sweep 50 to 95), median')
    for name in ['lake-original.jpg', 'lake-double-jpeg.jpg', 'ribnica-lake-camera.jpg',
                 'polyhaven-rocks-lossless.png', 'lake-splice-plane.jpg']:
        q = 50 + gray(name, 'ghostmin') * 45
        line = '  %-32s whole image %.0f' % (name, np.median(q))
        if name == 'lake-splice-plane.jpg':
            line += ', in the pasted plane %.0f (share at 60 or below %.0f %%, elsewhere %.0f %%)' % (
                np.median(q[sp]), 100 * (q[sp] <= 62.5).mean(), 100 * (q[~sp] <= 62.5).mean())
        say(line)
    for name in ['lake-original.jpg', 'lake-double-jpeg.jpg']:
        say('  %-32s normalised difference at 70: mean %.3f' % (name, gray(name, 'ghost70').mean()))

    say('== Luminance Gradient (normal map): mean red (0.5 flat; below 0.5 darker to the right)')
    eg = mask('eggs-mirrored-egg-mask.png')
    # the other large eggs in the front row, inner ellipses
    others = {'front left': (810, 1600, 200, 200), 'front third': (2330, 1850, 210, 200),
              'front right': (3180, 2030, 240, 240)}
    for name in ['eggs-original.jpg', 'eggs-mirrored-egg.jpg']:
        r = result(name, 'grad')[..., 0]
        parts = ['mirrored egg %.4f' % r[eg].mean()]
        for k, (cx, cy, rx, ry) in others.items():
            parts.append('%s %.4f' % (k, r[ellipse(r.shape, cx, cy, rx, ry)].mean()))
        say('  %-24s %s' % (name, ', '.join(parts)))

    say('== Clone Detection (mask): share marked')
    with open(os.path.join(MASKS, 'regions.json')) as f:
        regions = json.load(f)
    for name, label, op in CASES:
        if label != 'clone':
            continue
        c = gray(name, 'clone') > 0.5
        line = '  %-32s whole image %.2f %%' % (name, 100 * c.mean())
        if name == 'lake-clone.jpg':
            rg = regions[name]
            parts = []
            for k in ('source', 'copy'):
                x, y, s, _ = rg[k]
                parts.append('%s %.0f %%' % (k, 100 * c[y:y + s, x:x + s].mean()))
            rest = np.ones_like(c)
            for k in ('source', 'copy'):
                x, y, s, _ = rg[k]
                rest[y:y + s, x:x + s] = False
            parts.append('rest %.2f %%' % (100 * c[rest].mean()))
            line += ': ' + ', '.join(parts)
        say(line)

    with open(os.path.join(OUT, 'measure.txt'), 'w') as f:
        f.write('\n'.join(rep) + '\n')
    if '--png' in sys.argv[1:]:
        for name, label, op in CASES:
            a = result(name, label)
            im = Image.fromarray(np.clip(np.rint(a * 255), 0, 255).astype(np.uint8))
            im.thumbnail((1200, 1200))
            im.save(out_path(name, label)[:-4] + '.png')
    if '--keep' not in sys.argv[1:] and '--png' not in sys.argv[1:]:
        for name, label, op in CASES:
            os.remove(out_path(name, label))
    print('written to', os.path.join(OUT, 'measure.txt'))


if __name__ == '__main__':
    main()
