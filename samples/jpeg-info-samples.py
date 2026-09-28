#!/usr/bin/env python3
# The JPEG Info plug-in's report on every sample JPEG, as the plug-in makes
# it (jpeg_report.py, and the thumbnail comparison of jpeg-info.py with
# GdkPixbuf), and the double JPEG map of the lake photos. Runs in the
# Python of the GIMP Flatpak; samples/measure.py starts it (isolated, as
# the tests) and adds its lines to tests/output/samples/measure.txt.
#
#   jpeg-info-samples.py <samples/images>
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import glob
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.join(os.path.dirname(HERE), 'plug-ins', 'jpeg-info')
sys.path.insert(0, PLUGIN)
import jpeg_report as jr  # noqa: E402

spec = importlib.util.spec_from_file_location('jpeg_info', os.path.join(PLUGIN, 'jpeg-info.py'))
ji = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ji)

images = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'images')
print('== JPEG Info: quality of the last save, JPEGsnoop class, signature matches, '
      'double compression, thumbnail')
for path in sorted(glob.glob(os.path.join(images, '*.jpg'))):
    t0 = time.monotonic()
    r = jr.analyze(path)
    thumb = ''
    if r.get('thumbnail'):
        tb = ji.exif_thumbnail(path)
        ji.compare_thumbnail(path, tb, r)
        t = r['thumbnail']
        c = t.get('compare')
        thumb = ', thumbnail %sx%s%s' % (t.get('width'), t.get('height'), (
            ' box %s, %.1f levels apart, %.1f %% over 32' % (
                t.get('box'), c['mean'], 100 * c['share_over_32'])) if c else '')
    q = r['quality']
    d = r.get('double') or {}
    cams = [m for m in r['signatures']['matches'] if m['kind'] == 'cam']
    same = [m for m in cams if m['make_model_matches']]
    exif = (r['metadata'].get('exif') or {}).get('camera') or {}
    print('  %-30s quality %s%s, %s; class %d; %d cameras%s, %d programs; double %s%s%s; '
          '%.1f s' % (
              os.path.basename(path), q['estimate'], ' (exact %s)' % (
                  'IJG' if q.get('family_index') == 0 else q.get('family')) if q['exact']
              else ' (estimated)', r['frame']['subsampling'], r['assessment']['class'],
              len(cams), ' incl. the Exif camera %s %s' % (exif.get('Make'), exif.get('Model'))
              if same else '', len(r['signatures']['matches']) - len(cams), d.get('verdict'),
              ' (first save about %d)' % d['primary']['quality'] if d.get('primary') else '',
              thumb, time.monotonic() - t0))

print('== Double JPEG map: mean (255 saved twice) inside and outside the edited region')
MASKS = os.path.join(images, 'masks')
for name, mask in (('lake-splice-plane.jpg', 'lake-splice-plane-mask.png'),
                   ('lake-original.jpg', 'lake-splice-plane-mask.png'),
                   ('lake-double-jpeg.jpg', None), ('lake-clone.jpg', 'lake-clone-mask.png'),
                   ('sogndal-composite.jpg', None), ('ptz-montage.jpg', None),
                   ('pin-concealed.jpg', None)):
    t0 = time.monotonic()
    m = jr.map_of_file(os.path.join(images, name))
    if not m.get('map'):
        print('  %-30s no map (%s)' % (name, m.get('error') or 'no frequency qualifies'))
        continue
    bw, rows = m['bw'], m['rows']
    line = '  %-30s %d frequencies, %.1f %% of the blocks look saved twice' % (
        name, m['used'], 100 * m['share_double'])
    if mask:
        # the mask's blocks: GdkPixbuf reads the PNG
        pb = ji.GdkPixbuf.Pixbuf.new_from_file(os.path.join(MASKS, mask))
        mw, mh, stride, nch = pb.get_width(), pb.get_height(), pb.get_rowstride(), pb.get_n_channels()
        px = pb.get_pixels()
        inside, outside = [], []
        for by in range(rows):
            for bx in range(bw):
                x, y = 8 * bx + 4, 8 * by + 4
                v = m['map'][by * bw + bx]
                if x < mw and y < mh and px[y * stride + x * nch] > 127:
                    inside.append(v)
                else:
                    outside.append(v)
        line += '; region %.1f (%d blocks, %.0f %% at 128 or more), rest %.2f' % (
            sum(inside) / max(len(inside), 1), len(inside),
            100 * sum(v >= 128 for v in inside) / max(len(inside), 1),
            sum(outside) / max(len(outside), 1))
    print(line + '; %.1f s' % (time.monotonic() - t0))
