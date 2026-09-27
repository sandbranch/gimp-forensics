# Runs inside GIMP (tests/gimp-check.sh): applies the forensics operations
# to synthetic images as non-destructive filters, checks that they are
# filters, saves each image as XCF with its filter and loads it again,
# merges the filters and compares the pixels with the same operation run
# in plain GEGL in this process. For Error Level Analysis also against the
# way the GIMP 2 scripts did it: export as JPEG with GIMP's own exporter,
# load it, take the difference. Writes the scene and GIMP's results as
# float TIFF for the comparison with the gegl command line in
# gimp-check.sh. Prints PASS or FAIL per check.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import array
import math
import os
import random

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.environ['FORENSICS_CHECK_OUT']
W, H = 320, 240
FMT = "R'G'B'A float"
failed = []


def result(name, ok, msg=''):
    print('%s  gimp_%s%s' % ('PASS' if ok else 'FAIL', name, (': ' + msg) if msg else ''))
    if not ok:
        failed.append(name)


def scene():
    """a photo-like scene with 8 bit values (so 8 bit and float images hold
    the same), a pasted square of another texture and a cloned patch"""
    rnd = random.Random(1)
    a = array.array('f')
    for y in range(H):
        for x in range(W):
            u, v = x / (W - 1), y / (H - 1)
            if v < 0.45 + 0.05 * math.sin(7 * u):
                p = [0.4 + 0.3 * v, 0.6 + 0.2 * v, 0.9 - 0.1 * v]
            else:
                t = 0.5 + 0.25 * math.sin(x * 0.9) * math.sin(y * 0.7)
                p = [0.25 + 0.35 * t, 0.35 + 0.3 * t, 0.1 + 0.2 * t]
            if 0.6 < u < 0.8 and 0.55 < v < 0.85:
                p = [0.8, 0.15 + 0.1 * math.sin(x * 2.1), 0.1]
            p = [min(max(c + rnd.gauss(0, 3 / 255), 0.0), 1.0) for c in p]
            a.extend([round(c * 255) / 255 for c in p] + [1.0])
    # a cloned patch: 48 x 48 of the ground copied 100 pixels to the right
    for y in range(160, 208):
        a[(y * W + 130) * 4:(y * W + 178) * 4] = a[(y * W + 30) * 4:(y * W + 78) * 4]
    return a


def gegl_result(source, op, props):
    """the operation on the buffer source in plain GEGL, as R'G'B'A float"""
    rect = Gegl.Rectangle.new(0, 0, W, H)
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', source)
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


def save_tiff(path, data):
    rect = Gegl.Rectangle.new(0, 0, W, H)
    buf = Gegl.Buffer.new(FMT, 0, 0, W, H)
    buf.set(rect, FMT, data.tobytes())
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', buf)
    save = g.create_child('gegl:tiff-save')
    save.set_property('path', path)
    src.link(save)
    save.process()


def layer_pixels(layer):
    rect = Gegl.Rectangle.new(0, 0, W, H)
    return array.array('f', layer.get_buffer().get(rect, 1.0, FMT, Gegl.AbyssPolicy.NONE))


def max_diff(a, b, alpha=True):
    return max((abs(p - q) for i, (p, q) in enumerate(zip(a, b)) if alpha or i % 4 != 3),
               default=float('inf'))


def new_image(precision, data):
    image = Gimp.Image.new_with_precision(W, H, Gimp.ImageBaseType.RGB, precision)
    layer = Gimp.Layer.new(image, 'scene', W, H, Gimp.ImageType.RGBA_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, W, H), FMT, data.tobytes())
    buf.flush()
    layer.update(0, 0, W, H)
    return image, layer


def add_filter(layer, op, props):
    f = Gimp.DrawableFilter.new(layer, op, op)
    cfg = f.get_config()
    for k, v in props.items():
        cfg.set_property(k, v)
    f.update()
    layer.append_filter(f)
    return f


# GIMP's config takes enum values by their nick, GEGL by their number
ENUMS = {
    'forensics:error-level': {'chroma': ['4:2:0', '4:2:2', '4:4:4'],
                              'mode': ['color', 'luminance', 'maximum']},
    'forensics:jpeg-ghost': {'chroma': ['4:2:0', '4:2:2', '4:4:4'],
                             'mode': ['normalized', 'difference', 'minimum']},
    'forensics:noise': {'method': ['median', 'wavelet'],
                        'mode': ['color', 'luminance', 'signed']},
    'forensics:luminance-gradient': {'mode': ['normal', 'direction', 'magnitude']},
    'forensics:clone-detect': {'mode': ['overlay', 'mask']},
    'forensics:pca': {'mode': ['projection', 'distance']},
}


def gegl_props(op, props):
    enums = ENUMS.get(op, {})
    return {k: enums[k].index(v) if k in enums else v for k, v in props.items()}


Gegl.init(None)
data = scene()
save_tiff(os.path.join(OUT, 'scene.tif'), data)

U8 = Gimp.Precision.U8_NON_LINEAR
F32 = Gimp.Precision.FLOAT_NON_LINEAR
U16 = Gimp.Precision.U16_NON_LINEAR
# label, operation, precision, settings, tolerance
cases = [
    ('ela_float_defaults', 'forensics:error-level', F32, {}, 1e-6),
    ('ela_8_bit', 'forensics:error-level', U8, {}, 0.5 / 255 + 1e-6),
    ('ela_16_bit', 'forensics:error-level', U16, {}, 0.5 / 65535 + 1e-6),
    ('ela_float_options', 'forensics:error-level', F32,
     {'quality': 75, 'scale': 12.5, 'chroma': '4:4:4', 'grid-x': 4, 'grid-y': 11,
      'mode': 'luminance'}, 1e-6),
    ('ela_float_auto_levels', 'forensics:error-level', F32, {'auto-levels': True}, 1e-6),
    ('ghost_float_defaults', 'forensics:jpeg-ghost', F32, {}, 1e-6),
    ('ghost_8_bit', 'forensics:jpeg-ghost', U8, {}, 0.5 / 255 + 1e-6),
    ('ghost_float_options', 'forensics:jpeg-ghost', F32,
     {'quality': 60, 'sweep-min': 40, 'sweep-max': 80, 'sweep-step': 10,
      'block-size': 8, 'mode': 'minimum', 'colormap': True, 'grid-x': 3}, 1e-6),
    ('noise_float_defaults', 'forensics:noise', F32, {}, 1e-6),
    ('noise_16_bit', 'forensics:noise', U16, {}, 0.5 / 65535 + 1e-6),
    ('noise_float_options', 'forensics:noise', F32,
     {'method': 'wavelet', 'mode': 'luminance', 'amplitude': 25.0, 'average': 9}, 1e-6),
    ('noise_float_auto_levels', 'forensics:noise', F32,
     {'radius': 2, 'mode': 'signed', 'auto-levels': True}, 1e-6),
    ('gradient_float_defaults', 'forensics:luminance-gradient', F32, {}, 1e-6),
    ('gradient_8_bit', 'forensics:luminance-gradient', U8, {}, 0.5 / 255 + 1e-6),
    ('gradient_float_direction', 'forensics:luminance-gradient', F32,
     {'mode': 'direction', 'intensity': 20.0}, 1e-6),
    ('clone_float_defaults', 'forensics:clone-detect', F32, {}, 1e-6),
    ('clone_8_bit_mask', 'forensics:clone-detect', U8,
     {'mode': 'mask', 'block-size': 12, 'min-matches': 20}, 0.5 / 255 + 1e-6),
    ('pca_float_defaults', 'forensics:pca', F32, {}, 1e-6),
    ('pca_float_distance', 'forensics:pca', F32,
     {'component': 1, 'mode': 'distance', 'scale': 2.0, 'invert': True}, 1e-6),
]
cli = {}

for label, op, precision, props, tol in cases:
    image, layer = new_image(precision, data)
    # plain GEGL on the layer's pixels, in the layer's own format
    want = gegl_result(layer.get_buffer(), op, gegl_props(op, props))
    if precision != F32:
        # an integer layer holds 0 to 1
        want = array.array('f', (min(max(v, 0.0), 1.0) for v in want))
    add_filter(layer, op, props)
    names = [x.get_operation_name() for x in layer.get_filters()]
    result(label + '_is_a_filter', names == [op], str(names))

    # the filter survives an XCF save and load, with its settings
    xcf = os.path.join(OUT, label + '.xcf')
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(xcf), None)
    loaded = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(xcf))
    ll = loaded.get_layers()[0]
    lf = ll.get_filters()
    same = len(lf) == 1 and lf[0].get_operation_name() == op
    kept = {}
    if same:
        cfg = lf[0].get_config()
        for k, v in props.items():
            got = cfg.get_property(k)
            kept[k] = got
            if isinstance(v, float):
                # XCF keeps the filter's numbers as 32 bit floats
                same = same and abs(got - v) < 1e-6
            else:
                same = same and got == v
    result(label + '_survives_xcf', same, 'settings after loading: %s' % kept)

    # the original pixels are still there under the filter
    orig = layer_pixels(layer)
    result(label + '_non_destructive', max_diff(orig, data) <= 0.5 / 255,
           'the layer keeps its pixels until the filter is merged')

    layer.merge_filters()
    got = layer_pixels(layer)
    d = max_diff(got, want)
    result(label + '_same_as_gegl', d <= tol, 'max difference %.2g' % d)
    ll.merge_filters()
    got2 = layer_pixels(ll)
    d2 = max_diff(got2, want)
    result(label + '_same_as_gegl_after_xcf', d2 <= tol, 'max difference %.2g' % d2)
    if precision == F32:
        save_tiff(os.path.join(OUT, 'gimp-' + label + '.tif'), got)
        cli[label] = (op, props)
    image.delete()
    loaded.delete()

# Error Level Analysis as the GIMP 2 scripts did it: export as JPEG
# (quality 90, 4:2:0, integer DCT), load, Difference, x 20. The exported
# file is the filter's JPEG copy bit for bit (checked by decoding it with
# libjpeg's defaults), but GIMP loads JPEG files with the float IDCT and
# the filter decodes with the integer one, so a level here and there
# differs.
image, layer = new_image(U8, data)
jpg = os.path.join(OUT, 'export.jpg')
proc = Gimp.get_pdb().lookup_procedure('file-jpeg-export')
cfg = proc.create_config()
cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
cfg.set_property('image', image)
cfg.set_property('file', Gio.File.new_for_path(jpg))
cfg.set_property('quality', 0.9)
cfg.set_property('smoothing', 0.0)
cfg.set_property('sub-sampling', 'sub-sampling-2x2')
cfg.set_property('dct', 'integer')
status = proc.run(cfg).index(0)
exported = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(jpg))
jl = exported.get_layers()[0]
rect = Gegl.Rectangle.new(0, 0, W, H)
jpix = array.array('f', jl.get_buffer().get(rect, 1.0, FMT, Gegl.AbyssPolicy.NONE))
add_filter(layer, 'forensics:error-level', {})
layer.merge_filters()
ela = layer_pixels(layer)
# (the 8 bit layer holds the result up to 1)
diffs = [abs(min(abs(data[i] - jpix[i]) * 20, 1.0) - ela[i]) * 255 / 20
         for i in range(len(data)) if i % 4 != 3]
mean = sum(diffs) / len(diffs)
result('ela_like_gimp_jpeg_export', status == Gimp.PDBStatusType.SUCCESS and
       mean < 0.1 and max(diffs) <= 4.5,
       'the difference with a JPEG exported by GIMP at 90 %%: the filter '
       'differs by %.3f levels on average, at most %.1f' % (mean, max(diffs)))
image.delete()
exported.delete()

with open(os.path.join(OUT, 'cli-cases.txt'), 'w') as f:
    for label, (op, props) in cli.items():
        f.write(label + ' ' + op + ' ' +
                ' '.join('%s=%s' % (k, str(v).lower() if isinstance(v, bool) else v)
                         for k, v in props.items()) + '\n')
with open(os.path.join(OUT, 'gimp-check.status'), 'w') as f:
    f.write('%d\n' % len(failed))
