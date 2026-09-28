# Runs inside GIMP (tests/jpeg-info/run.sh): makes the JPEG files the JPEG
# Info tests read, with GIMP's own JPEG export, into JI_OUT/fixtures:
#
#   q90.jpg            baseline, standard Huffman tables, 4:2:0, GIMP's Exif
#   q75-progressive.jpg  progressive, optimised tables
#   q95-444.jpg        4:4:4
#   q95-444-restart.jpg  the same with a restart marker every 2 MCU rows
#                      (jpegtran)
#   q85-progressive-restart.jpg  progressive with restart markers every 7
#                      MCUs (jpegtran)
#   q50-422.jpg        4:2:2
#   gray-q80.jpg       grayscale
#   double-70-90.jpg   saved at 70, opened, saved at 90 (the whole image)
#   double-50-85.jpg   50, then 85
#   double-90-75.jpg   90, then 75 (a first save finer than the last)
#   double-shifted.jpg 70, opened, 3 pixels cut off the left, saved at 90
#   splice-into-double.jpg  saved at 70, a region (x 320, y 160, 192 x
#                      160) of the scene never saved as JPEG pasted in,
#                      saved at 90
#                      (the grids do not line up)
#   camera-thumb.jpg   Exif as a camera writes it (make, model, maker notes)
#                      with a thumbnail of the image
#   thumb-edited.jpg   the same Exif and thumbnail on the image after a red
#                      rectangle was painted into it
#   thumb-cropped.jpg  the same on the image cropped to 70 % of its width
#   thumb-bars.jpg     a 720 x 480 image whose thumbnail has black bars
#   not-jpeg.png       a PNG
#
# The scene is generated (value noise, no photograph), 640 x 480.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import os
import random
import struct
import subprocess

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.path.join(os.environ['JI_OUT'], 'fixtures')
os.makedirs(OUT, exist_ok=True)
W, H = 640, 480
FMT = "R'G'B' float"


def value_noise(seed, scale):
    rnd = random.Random(seed)
    gw, gh = W // scale + 2, H // scale + 2
    grid = [[rnd.random() for _ in range(gw)] for _ in range(gh)]

    def at(x, y):
        fx, fy = x / scale, y / scale
        ix, iy = int(fx), int(fy)
        tx, ty = fx - ix, fy - iy
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = grid[iy][ix] * (1 - tx) + grid[iy][ix + 1] * tx
        b = grid[iy + 1][ix] * (1 - tx) + grid[iy + 1][ix + 1] * tx
        return a * (1 - ty) + b * ty
    return at


def scene():
    rnd = random.Random(5)
    clouds, fine, coarse = value_noise(11, 40), value_noise(12, 3), value_noise(13, 9)
    data = bytearray()
    for y in range(H):
        for x in range(W):
            u, v = x / (W - 1), y / (H - 1)
            if v < 0.4 + 0.05 * math.sin(6 * u):
                t = clouds(x, y * 2)
                p = [0.4 + 0.3 * v + 0.12 * t, 0.6 + 0.2 * v + 0.12 * t, 0.92 - 0.1 * v + 0.05 * t]
            else:
                t = 0.6 * fine(x, y) + 0.4 * coarse(x, y)
                p = [0.25 + 0.35 * t, 0.35 + 0.3 * t, 0.1 + 0.2 * t]
            data += struct.pack('fff', *[min(max(c + rnd.gauss(0, 3 / 255), 0.0), 1.0)
                                         for c in p])
    return bytes(data)


def new_image(data, gray=False):
    image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'scene', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, W, H), FMT, data)
    buf.flush()
    if gray:
        image.convert_grayscale()
    return image


def export(image, name, quality, sub='sub-sampling-2x2', progressive=False, optimize=False,
           thumbnail=True):
    path = os.path.join(OUT, name)
    proc = Gimp.get_pdb().lookup_procedure('file-jpeg-export')
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    cfg.set_property('file', Gio.File.new_for_path(path))
    cfg.set_property('quality', quality / 100.0)
    cfg.set_property('sub-sampling', sub)
    cfg.set_property('progressive', progressive)
    cfg.set_property('optimize', optimize)
    cfg.set_property('include-exif', thumbnail)
    cfg.set_property('include-thumbnail', thumbnail)
    cfg.set_property('include-comment', False)
    r = proc.run(cfg)
    if r.index(0) != Gimp.PDBStatusType.SUCCESS:
        raise RuntimeError('export of %s failed' % name)
    return path


def load(path):
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))


data = scene()
img = new_image(data)
export(img, 'q90.jpg', 90)
export(img, 'q75-progressive.jpg', 75, progressive=True, optimize=True)
# restart markers: GIMP 3.2.6 writes them only from its dialog (its
# use-restart is an auxiliary argument that a script cannot set), so
# jpegtran of the Flatpak adds them, losslessly
p = export(img, 'q95-444.jpg', 95, sub='sub-sampling-1x1')
subprocess.run(['jpegtran', '-restart', '2', '-copy', 'all', '-outfile',
                os.path.join(OUT, 'q95-444-restart.jpg'), p], check=True)
p = export(img, 'q85.jpg', 85)
subprocess.run(['jpegtran', '-restart', '7B', '-progressive', '-copy', 'all', '-outfile',
                os.path.join(OUT, 'q85-progressive-restart.jpg'), p], check=True)
os.remove(p)
export(img, 'q50-422.jpg', 50, sub='sub-sampling-2x1')
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, img, Gio.File.new_for_path(
    os.path.join(OUT, 'not-jpeg.png')), None)
gray = new_image(data, True)
export(gray, 'gray-q80.jpg', 80)
gray.delete()

for q1, q2, name in ((70, 90, 'double-70-90.jpg'), (50, 85, 'double-50-85.jpg'),
                     (90, 75, 'double-90-75.jpg')):
    first = export(img, 'first-%d.jpg' % q1, q1, thumbnail=False)
    again = load(first)
    export(again, name, q2)
    again.delete()
# a region never saved as JPEG pasted into an image saved at 70, the whole
# saved at 90 (the textbook splice: the rest saved twice, the region once)
first = load(os.path.join(OUT, 'first-70.jpg'))
fl = first.get_layers()[0]
region = Gegl.Rectangle.new(320, 160, 192, 160)
fl.get_buffer().set(region, FMT, img.get_layers()[0].get_buffer().get(region, 1.0, FMT,
                                                                       Gegl.AbyssPolicy.NONE))
fl.get_buffer().flush()
export(first, 'splice-into-double.jpg', 90, thumbnail=False)
first.delete()
first = load(os.path.join(OUT, 'first-70.jpg'))
first.crop(W - 3, H, 3, 0)
export(first, 'double-shifted.jpg', 90)
first.delete()

# Exif with a thumbnail, as a camera writes it (GIMP writes none when it
# runs without a display): a camera's make and model, maker notes, and a
# 160 x 120 thumbnail of the image, saved at quality 80 by GIMP
def tiff_ifd(entries, start, next_ifd):
    """a little endian IFD at offset start: entries (tag, type, count,
    bytes); returns its bytes, values that do not fit after it"""
    n = len(entries)
    data_at = start + 2 + 12 * n + 4
    head = struct.pack('<H', n)
    tail = b''
    for tag, typ, count, value in sorted(entries):
        if len(value) <= 4:
            head += struct.pack('<HHI', tag, typ, count) + value.ljust(4, b'\x00')
        else:
            head += struct.pack('<HHII', tag, typ, count, data_at + len(tail))
            tail += value + (b'\x00' if len(value) % 2 else b'')
    return head + struct.pack('<I', next_ifd) + tail


def exif_segment(make, model, software, thumb):
    """APP1 Exif: IFD0, an Exif IFD with maker notes, IFD1 with the thumbnail"""
    def ascii(s):
        b = s.encode() + b'\x00'
        return (2, len(b), b)
    note = b'Nikon\x00' + bytes(range(64))
    # sizes first, offsets after (a fixed layout)
    ifd0_at = 8
    ifd0 = [(0x010F,) + ascii(make), (0x0110,) + ascii(model), (0x0131,) + ascii(software),
            (0x8769, 4, 1, b'\x00' * 4)]
    size0 = len(tiff_ifd(ifd0, ifd0_at, 0))
    exif_at = ifd0_at + size0
    exif_ifd = [(0x927C, 7, len(note), note)]
    size_e = len(tiff_ifd(exif_ifd, exif_at, 0))
    ifd1_at = exif_at + size_e
    ifd1 = [(0x0103, 3, 1, struct.pack('<H', 6)), (0x0201, 4, 1, b'\x00' * 4),
            (0x0202, 4, 1, struct.pack('<I', len(thumb)))]
    size1 = len(tiff_ifd(ifd1, ifd1_at, 0))
    thumb_at = ifd1_at + size1
    ifd0[3] = (0x8769, 4, 1, struct.pack('<I', exif_at))
    ifd1[1] = (0x0201, 4, 1, struct.pack('<I', thumb_at))
    tiff = (b'II*\x00' + struct.pack('<I', ifd0_at) + tiff_ifd(ifd0, ifd0_at, ifd1_at) +
            tiff_ifd(exif_ifd, exif_at, 0) + tiff_ifd(ifd1, ifd1_at, 0) + thumb)
    body = b'Exif\x00\x00' + tiff
    return b'\xff\xe1' + struct.pack('>H', len(body) + 2) + body


def with_exif(path, exif):
    """puts the Exif segment right after SOI (the file has none of its own)"""
    d = open(path, 'rb').read()
    open(path, 'wb').write(d[:2] + exif + d[2:])


def thumbnail_of(image, name, bars=False):
    """a 160 x 120 JPEG of the image (letterboxed with black bars if its
    shape is not 4:3, as cameras do)"""
    t = image.duplicate()
    t.flatten()
    w, h = t.get_width(), t.get_height()
    tw, th = (160, round(160 * h / w)) if w * 3 >= h * 4 else (round(120 * w / h), 120)
    t.scale(tw, th)
    if bars:
        t.resize(160, 120, (160 - tw) // 2, (120 - th) // 2)
        Gimp.context_set_background(Gegl.Color.new('black'))
        t.flatten()
    p = export(t, name, 80, thumbnail=False)
    t.delete()
    data = open(p, 'rb').read()
    os.remove(p)
    return data


camera = ('NIKON', 'COOLPIX L23', 'COOLPIX L23 V1.1')
thumb = thumbnail_of(img, 'thumb.jpg')
p = export(img, 'camera-thumb.jpg', 90, thumbnail=False)
with_exif(p, exif_segment(*camera, thumb))

# the same thumbnail on other pixels: a red rectangle painted in, and the
# image cropped to 70 % of its width
edited = new_image(data)
Gimp.Image.select_rectangle(edited, Gimp.ChannelOps.REPLACE, 200, 150, 240, 180)
Gimp.context_set_foreground(Gegl.Color.new('red'))
edited.get_layers()[0].edit_fill(Gimp.FillType.FOREGROUND)
Gimp.Selection.none(edited)
p = export(edited, 'thumb-edited.jpg', 90, thumbnail=False)
with_exif(p, exif_segment(*camera, thumb))
edited.delete()
cropped = new_image(data)
cropped.crop(int(W * 0.7), H, 0, 0)
p = export(cropped, 'thumb-cropped.jpg', 90, thumbnail=False)
with_exif(p, exif_segment(*camera, thumb))
cropped.delete()
# a 3:2 image: its thumbnail has black bars above and below
wide = new_image(data)
wide.scale(720, 480)
p = export(wide, 'thumb-bars.jpg', 90, thumbnail=False)
with_exif(p, exif_segment(*camera, thumbnail_of(wide, 'thumb.jpg', bars=True)))
wide.delete()
img.delete()
for q in (70, 50, 90):
    os.remove(os.path.join(OUT, 'first-%d.jpg' % q))
print('JI fixtures written to', OUT)
