# Runs inside GIMP without a window (tests/jpeg-info/run.sh): the JPEG Info
# plug-in (plug-in-forensics-jpeg-info) on the fixtures of make-fixtures.py,
# non-interactively: the report it returns (JSON), the thumbnail
# comparison it does with GdkPixbuf, and the thumbnail layers it adds
# (add-thumbnail). Prints PASS or FAIL per check and "JI GIMP failures: N".
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import array
import json
import os

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.environ['JI_OUT']
FIX = os.path.join(OUT, 'fixtures')
PROC = 'plug-in-forensics-jpeg-info'
FMT = "R'G'B'A float"
failures = []


def check(name, ok, msg=''):
    print('%s  ji_gimp_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failures.append(name)


def info(image, add=False):
    proc = Gimp.get_pdb().lookup_procedure(PROC)
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    cfg.set_property('add-thumbnail', add)
    res = proc.run(cfg)
    status = res.index(0)
    rep = json.loads(res.index(1)) if status == Gimp.PDBStatusType.SUCCESS else None
    return status, rep


def load(name):
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(os.path.join(FIX, name)))


def mean_of(drawable, rect=None):
    w, h = drawable.get_width(), drawable.get_height()
    x, y, rw, rh = rect or (0, 0, w, h)
    px = array.array('f', drawable.get_buffer().get(Gegl.Rectangle.new(x, y, rw, rh), 1.0, FMT,
                                                    Gegl.AbyssPolicy.NONE))
    return sum(v for i, v in enumerate(px) if i % 4 != 3) / (3 * rw * rh) * 255


def hints(rep, level):
    return [t for lv, t in rep['hints'] if lv == level]


Gegl.init(None)

image = load('q90.jpg')
status, rep = info(image)
check('runs', status == Gimp.PDBStatusType.SUCCESS and rep and rep['is_jpeg'], str(status))
check('quality_90', rep['quality']['estimate'] == 90 and rep['quality']['exact'],
      str(rep['quality']['estimate']))
check('no_thumbnail_no_group', rep['thumbnail'] is None and rep['thumbnail_group'] is None and
      len(image.get_layers()) == 1, None)
image.delete()

image = load('camera-thumb.jpg')
status, rep = info(image)
c = (rep['thumbnail'] or {}).get('compare') or {}
check('thumbnail_like_the_image', c and c['mean'] < 6 and hints(rep, 'ok') and
      not any('thumbnail' in t for t in hints(rep, 'edit')),
      'mean difference %.2f levels, %.2f %% over 32' % (c.get('mean', -1),
                                                       100 * c.get('share_over_32', -1)))
photo = image.get_layers()[0]
before = mean_of(photo)
status, rep2 = info(image, True)
layers = image.get_layers()
group = layers[0]
kids = group.get_children() if group.is_group() else []
check('thumbnail_layers', status == Gimp.PDBStatusType.SUCCESS and group.get_name() ==
      'EXIF Thumbnail' and rep2['thumbnail_group'] == group.get_id() and len(kids) == 2 and
      kids[0].get_mode() == Gimp.LayerMode.DIFFERENCE and
      (kids[0].get_width(), kids[0].get_height()) == (640, 480) and layers[1] == photo and
      abs(mean_of(photo) - before) < 1e-6,
      'group %s, layers %s' % (group.get_name(), [(k.get_name(), k.get_width(), k.get_height())
                                                  for k in kids]))
flat = Gimp.Layer.new_from_visible(image, image, 'flat')
check('thumbnail_difference_small', mean_of(flat) < 8, 'the group shows the difference: mean '
      '%.2f levels' % mean_of(flat))
image.delete()

image = load('thumb-edited.jpg')
status, rep = info(image, True)
c = rep['thumbnail']['compare']
edit = [t for t in hints(rep, 'edit') if 'thumbnail differs' in t]
flat = Gimp.Layer.new_from_visible(image, image, 'flat')
inside = mean_of(flat, (220, 170, 200, 140))
outside = mean_of(flat, (20, 20, 150, 100))
check('edited_image_differs_from_thumbnail', bool(edit) and inside > 5 * max(outside, 1),
      '%.1f %% of the thumbnail\'s pixels over 32 levels; the Difference layers: %.1f levels in '
      'the painted rectangle, %.1f elsewhere' % (100 * c['share_over_32'], inside, outside))
image.delete()

image = load('thumb-cropped.jpg')
status, rep = info(image)
check('cropped_image_other_shape', any('another shape' in t for t in hints(rep, 'note')),
      '; '.join(t for t in hints(rep, 'note') if 'shape' in t))
image.delete()

image = load('thumb-bars.jpg')
status, rep = info(image)
t = rep['thumbnail']
x0, y0, x1, y1 = t['box']
check('black_bars_found', (x0, x1) == (0, 160) and 5 <= y0 <= 7 and 112 <= y1 <= 114 and
      t['compare']['mean'] < 6 and not any('another shape' in s for s in hints(rep, 'note')),
      'content %s, mean difference %.2f' % (t['box'], t['compare']['mean']))
image.delete()

image = load('double-70-90.jpg')
status, rep = info(image)
check('double_compression', rep['double']['verdict'] == 'likely' and
      rep['suggest'] == {'ela_quality': 90, 'ghost_quality': 70}, str(rep['suggest']))
image.delete()

image = load('splice-into-double.jpg')
proc = Gimp.get_pdb().lookup_procedure(PROC)
cfg = proc.create_config()
cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
cfg.set_property('image', image)
cfg.set_property('add-double-map', True)
res = proc.run(cfg)
rep = json.loads(res.index(1))
top = image.get_layers()[0]
inside = mean_of(top, (340, 180, 150, 120))
outside = mean_of(top, (20, 20, 250, 120))
check('double_map_layer', rep.get('double_map_layer') == top.get_id() and
      top.get_name() == 'Double JPEG Map' and (top.get_width(), top.get_height()) == (640, 480) and
      inside < 40 and outside > 200,
      'layer %s %dx%d: %.0f in the pasted region, %.0f elsewhere' % (
          top.get_name(), top.get_width(), top.get_height(), inside, outside))
image.delete()

image = load('not-jpeg.png')
status, rep = info(image)
check('png', status == Gimp.PDBStatusType.SUCCESS and not rep['is_jpeg'] and rep['error'],
      rep and rep['error'])
image.delete()
image = Gimp.Image.new(50, 40, Gimp.ImageBaseType.RGB)
status, rep = info(image)
check('image_without_file', status == Gimp.PDBStatusType.SUCCESS and not rep['is_jpeg'],
      rep and rep['error'])
image.delete()

print('JI GIMP failures: %d' % len(failures))
