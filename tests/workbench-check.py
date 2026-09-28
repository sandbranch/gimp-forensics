# Runs inside GIMP (tests/workbench-check.sh): the Forensics Workbench on
# synthetic images, without a window. Checks the Forensics group (at the
# top, its layers in order, each with its filter and settings, only the
# top one visible), that the image's own layers are untouched, that the
# group survives an XCF save and load, that the analyses' pixels are
# those of the operations on the flattened image, and a second run, a
# grayscale image, an image of several layers, and the error without
# any analysis. Prints PASS or FAIL per check.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import array
import math
import os
import random
import sys

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.environ['FORENSICS_CHECK_OUT']
W, H = 256, 192
FMT = "R'G'B'A float"
PROC = 'python-fu-forensics-workbench'
failed = []

DEFAULT_LAYERS = ['Error Level Analysis', 'JPEG Ghost', 'Noise Analysis',
                  'Wavelet Noise Map', 'Min/Max Deviation', 'Echo Edge Filter',
                  'Luminance Gradient', 'Clone Detection', 'Level Sweep',
                  'Principal Component 2', 'Principal Component 3']
DEFAULT_OPS = ['forensics:error-level', 'forensics:jpeg-ghost', 'forensics:noise',
               'forensics:wavelet-noise', 'forensics:minmax', 'forensics:echo',
               'forensics:luminance-gradient', 'forensics:clone-detect', 'gegl:levels',
               'forensics:pca', 'forensics:pca']
OFF = dict(ela=False, ghost=False, noise=False, gradient=False, clone=False, sweep=False,
           pca=False, hsv=False, lab=False, dqmap=False, wnoise=False, minmax=False, echo=False,
           median=False, resampling=False, bitplane=False, thumbnail=False)


def result(name, ok, msg=''):
    print('%s  workbench_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failed.append(name)


def scene():
    rnd = random.Random(3)
    a = array.array('f')
    for y in range(H):
        for x in range(W):
            if y < 80:
                p = [0.4 + 0.002 * y, 0.6, 0.9]
            else:
                t = 0.5 + 0.25 * math.sin(x * 0.9) * math.sin(y * 0.7) + \
                    0.25 * math.sin(x * 0.23 + y * 0.11)
                p = [0.25 + 0.35 * t, 0.35 + 0.3 * t, 0.1 + 0.2 * t]
            p = [min(max(c + rnd.gauss(0, 3 / 255), 0.0), 1.0) for c in p]
            a.extend([round(c * 255) / 255 for c in p] + [1.0])
    for y in range(120, 168):
        a[(y * W + 150) * 4:(y * W + 198) * 4] = a[(y * W + 20) * 4:(y * W + 68) * 4]
    return a


def pixels(drawable, w=W, h=H):
    rect = Gegl.Rectangle.new(0, 0, w, h)
    return array.array('f', drawable.get_buffer().get(rect, 1.0, FMT, Gegl.AbyssPolicy.NONE))


def new_image(data, precision=Gimp.Precision.U8_NON_LINEAR, base=Gimp.ImageBaseType.RGB):
    image = Gimp.Image.new_with_precision(W, H, base, precision)
    kind = Gimp.ImageType.RGBA_IMAGE if base == Gimp.ImageBaseType.RGB else Gimp.ImageType.GRAYA_IMAGE
    layer = Gimp.Layer.new(image, 'photo', W, H, kind, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, W, H), FMT, data.tobytes())
    buf.flush()
    layer.update(0, 0, W, H)
    return image, layer


def workbench(image, **settings):
    proc = Gimp.get_pdb().lookup_procedure(PROC)
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    for k, v in settings.items():
        cfg.set_property(k.replace('_', '-'), v)
    res = proc.run(cfg)
    return res.index(0)


def group_state(image):
    top = image.get_layers()[0]
    kids = top.get_children() if top.is_group() else []
    return top, [(k.get_name(), [f.get_operation_name() for f in k.get_filters()],
                  k.get_visible()) for k in kids]


def gegl_on(data, op, props):
    rect = Gegl.Rectangle.new(0, 0, W, H)
    src_buf = Gegl.Buffer.new(FMT, 0, 0, W, H)
    src_buf.set(rect, FMT, data.tobytes())
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', src_buf)
    node = g.create_child(op)
    for k, v in props.items():
        node.set_property(k, v)
    out = Gegl.Buffer.new(FMT, 0, 0, W, H)
    sink = g.create_child('gegl:write-buffer')
    sink.set_property('buffer', out)
    src.link(node)
    node.link(sink)
    sink.process()
    return array.array('f', out.get(rect, 1.0, FMT, Gegl.AbyssPolicy.NONE))


def max_diff(a, b):
    return max((abs(p - q) for p, q in zip(a, b)), default=float('inf'))


Gegl.init(None)
data = scene()

# 1. the defaults on an 8 bit image
image, photo = new_image(data)
before = pixels(photo)
status = workbench(image)
result('runs', status == Gimp.PDBStatusType.SUCCESS, str(status))
top, kids = group_state(image)
result('group_at_the_top', top.is_group() and top.get_name() == 'Forensics' and
       top.get_parasite('forensics-workbench') is not None,
       'top layer: %s' % top.get_name())
result('layers_and_filters', [k[0] for k in kids] == DEFAULT_LAYERS and
       [k[1] for k in kids] == [[op] for op in DEFAULT_OPS],
       '; '.join('%s: %s' % (k[0], ','.join(k[1])) for k in kids))
result('only_the_top_visible', [k[2] for k in kids] == [True] + [False] * (len(kids) - 1),
       str([k[2] for k in kids]))
layers = image.get_layers()
result('original_untouched', len(layers) == 2 and layers[1] == photo and
       photo.get_name() == 'photo' and not photo.get_filters() and
       max_diff(pixels(photo), before) == 0.0 and photo.get_visible(),
       'the photo layer: same pixels, no filters, still visible, under the group')

# the analyses' pixels: the operations on the flattened image (8 bit)
kid_layers = top.get_children()
checks = [(0, 'forensics:error-level', {'quality': 90, 'scale': 20.0}),
          (2, 'forensics:noise', {'amplitude': 10.0}),
          (4, 'forensics:minmax', {'mode': 1}),
          (5, 'forensics:echo', {}),
          (9, 'forensics:pca', {'component': 2})]
for i, op, props in checks:
    dup = image.duplicate()
    layer = dup.get_layers()[0].get_children()[i]
    layer.merge_filters()
    want = array.array('f', (min(max(v, 0.0), 1.0) for v in gegl_on(before, op, props)))
    d = max_diff(pixels(layer), want)
    result('pixels_%s' % kids[i][0].lower().replace(' ', '_').replace('/', '_'), d <= 0.5 / 255 + 1e-6,
           'merged filter against %s on the flattened image: max difference %.2g' % (op, d))
    dup.delete()

# 2. XCF
xcf = os.path.join(OUT, 'workbench.xcf')
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(xcf), None)
loaded = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(xcf))
ltop, lkids = group_state(loaded)
cfg = ltop.get_children()[0].get_filters()[0].get_config()
result('survives_xcf', ltop.get_name() == 'Forensics' and lkids == kids and
       cfg.get_property('quality') == 90 and abs(cfg.get_property('scale') - 20.0) < 1e-6 and
       ltop.get_parasite('forensics-workbench') is not None,
       '%d layers with their filters and settings after loading' % len(lkids))
lphoto = loaded.get_layers()[1]
result('original_untouched_after_xcf', max_diff(pixels(lphoto), before) == 0.0 and
       not lphoto.get_filters(), None)
loaded.delete()

# 3. again: a second group on top, the first one hidden
status = workbench(image, ela_quality=75, noise=False, clone=False, hsv=True, lab=True)
top2, kids2 = group_state(image)
groups = [l for l in image.get_layers() if l.is_group()]
names2 = [k[0] for k in kids2]
result('second_run', status == Gimp.PDBStatusType.SUCCESS and len(groups) == 2 and
       groups[0] == top2 and not groups[1].get_visible() and
       'Noise Analysis' not in names2 and 'Clone Detection' not in names2 and
       names2[-6:] == ['HSV Hue', 'HSV Saturation', 'HSV Value', 'LAB L', 'LAB A', 'LAB B'] and
       top2.get_children()[0].get_filters()[0].get_config().get_property('quality') == 75,
       'the new group on top, the first hidden; layers: %s' % ', '.join(names2))
image.delete()

# 4. a grayscale float image, and an image of several layers with offsets
gimage, glayer = new_image(data, Gimp.Precision.FLOAT_NON_LINEAR, Gimp.ImageBaseType.GRAY)
status = workbench(gimage)
gtop, gkids = group_state(gimage)
result('grayscale_float_image', status == Gimp.PDBStatusType.SUCCESS and
       [k[0] for k in gkids] == DEFAULT_LAYERS, str(status))
gimage.delete()

image, photo = new_image(data)
patch = Gimp.Layer.new(image, 'patch', 40, 30, Gimp.ImageType.RGBA_IMAGE, 100,
                       Gimp.LayerMode.NORMAL)
image.insert_layer(patch, None, 0)
patch.set_offsets(100, 50)
patch.get_buffer().set(Gegl.Rectangle.new(0, 0, 40, 30), FMT,
                       array.array('f', [0.9, 0.1, 0.1, 1.0] * (40 * 30)).tobytes())
patch.get_buffer().flush()
status = workbench(image, **dict(OFF, ela=True))
top, kids = group_state(image)
# the analysed copy holds the visible image (a layer's buffer is without
# its filters): the red patch on the photo
flat = pixels(top.get_children()[0])
i = (60 * W + 110) * 4
j = (10 * W + 10) * 4
result('several_layers_flattened', status == Gimp.PDBStatusType.SUCCESS and
       [k[0] for k in kids] == ['Error Level Analysis'] and len(image.get_layers()) == 3 and
       abs(flat[i] - 0.9) < 0.01 and abs(flat[i + 1] - 0.1) < 0.01 and
       max_diff(flat[j:j + 4], data[j:j + 4]) <= 0.5 / 255,
       'one analysis of the visible image: the red patch at 110,60 and the photo at 10,10')
image.delete()

# 5. nothing chosen: an error, nothing added
image, photo = new_image(data)
status = workbench(image, **OFF)
result('nothing_chosen_is_an_error', status == Gimp.PDBStatusType.CALLING_ERROR and
       len(image.get_layers()) == 1, str(status))
image.delete()

# 6. the optional analyses
image, photo = new_image(data)
status = workbench(image, **dict(OFF, median=True, resampling=True, bitplane=True))
top, kids = group_state(image)
result('optional_analyses', status == Gimp.PDBStatusType.SUCCESS and
       [k[0] for k in kids] == ['Median Filtering Detection', 'Resampling Detection',
                                'Bit Plane 0'] and
       [k[1] for k in kids] == [['forensics:median-detect'], ['forensics:resampling'],
                                ['forensics:bit-plane']], str(kids))
image.delete()

# 7. an image opened from a JPEG file (saved at 70, then at 90, with an
# Exif thumbnail): JPEG Info's layers go into the group, and ELA and JPEG
# Ghost take the file's qualities
sys.path.insert(0, os.path.join(os.environ['FORENSICS_SRC'], 'tests', 'jpeg-info'))
from exif_fixture import exif_segment, with_exif  # noqa: E402


def export_jpeg(image, path, quality):
    proc = Gimp.get_pdb().lookup_procedure('file-jpeg-export')
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    cfg.set_property('file', Gio.File.new_for_path(path))
    cfg.set_property('quality', quality / 100.0)
    cfg.set_property('sub-sampling', 'sub-sampling-2x2')
    cfg.set_property('include-exif', False)
    cfg.set_property('include-thumbnail', False)
    proc.run(cfg)


def load(path):
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))


image, photo = new_image(data)
export_jpeg(image, os.path.join(OUT, 'first.jpg'), 70)
image.delete()
first = load(os.path.join(OUT, 'first.jpg'))
export_jpeg(first, os.path.join(OUT, 'photo.jpg'), 90)
first.scale(160, 120)
export_jpeg(first, os.path.join(OUT, 'thumb.jpg'), 80)
first.delete()
with_exif(os.path.join(OUT, 'photo.jpg'),
          exif_segment('NIKON', 'COOLPIX L23', 'COOLPIX L23 V1.1',
                       open(os.path.join(OUT, 'thumb.jpg'), 'rb').read()))
image = load(os.path.join(OUT, 'photo.jpg'))
status = workbench(image)
top, kids = group_state(image)
names = [k[0] for k in kids]
kid_layers = top.get_children()
ela = kid_layers[0].get_filters()[0].get_config().get_property('quality')
ghost = kid_layers[1].get_filters()[0].get_config().get_property('quality')
thumb = kid_layers[-1]
result('jpeg_file_layers', status == Gimp.PDBStatusType.SUCCESS and
       names[:3] == ['Error Level Analysis', 'JPEG Ghost', 'Double JPEG Map'] and
       names[-1] == 'EXIF Thumbnail' and thumb.is_group() and len(thumb.get_children()) == 2 and
       len(image.get_layers()) == 2 and [k[2] for k in kids].count(True) == 1,
       '; '.join(names))
result('jpeg_file_qualities', ela == 90 and ghost == 70,
       'Error Level Analysis at %s (the last save), JPEG Ghost at %s (the first)' % (ela, ghost))
image.delete()
image = load(os.path.join(OUT, 'photo.jpg'))
status = workbench(image, jpeg_suggest=False, dqmap=False, thumbnail=False, ela_quality=75,
                   ghost_quality=60)
top, kids = group_state(image)
ela = top.get_children()[0].get_filters()[0].get_config().get_property('quality')
result('jpeg_file_without_suggestions', status == Gimp.PDBStatusType.SUCCESS and ela == 75 and
       top.get_children()[1].get_filters()[0].get_config().get_property('quality') == 60 and
       'Double JPEG Map' not in [k[0] for k in kids], 'the qualities given (75 and 60)')
image.delete()

with open(os.path.join(OUT, 'workbench-check.status'), 'w') as f:
    f.write('%d\n' % len(failed))
