# Runs inside GIMP on a Broadway display (tests/gui/start.sh): makes a
# synthetic "edited photo" and shows it, for looking at the forensics
# filters and the Forensics Workbench in GIMP's own dialogs.
#
# The photo: a generated scene saved as a quality 90 JPEG with GIMP's
# exporter and opened again (the camera's file); into it a region of
# another scene from a quality 60 JPEG (a paste from another picture),
# and a patch of the ground copied to another place (a clone), saved as a
# JPEG at 90 (FORENSICS_GUI_OUT/edited.jpg) and opened: the image shown.
# Written to FORENSICS_GUI_OUT/edited.png too. GIMP stays open.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import os
import random
import struct

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.environ['FORENSICS_GUI_OUT']
W, H = 480, 320
FMT = "R'G'B' float"


def value_noise(seed, scale):
    """smooth random values (not periodic, unlike sums of sines, which
    Clone Detection rightly finds everywhere): a lattice of random values
    every scale pixels, interpolated"""
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


def scene(seed):
    rnd = random.Random(seed)
    clouds = value_noise(seed * 10 + 1, 40)
    fine = value_noise(seed * 10 + 2, 3)
    coarse = value_noise(seed * 10 + 3, 9)
    data = bytearray()
    for y in range(H):
        for x in range(W):
            u, v = x / (W - 1), y / (H - 1)
            if v < 0.45 + 0.05 * math.sin(6 * u + seed):
                p = [0.4 + 0.3 * v, 0.6 + 0.2 * v, 0.92 - 0.1 * v]
                t = clouds(x, y * 2)
                p = [p[0] + 0.12 * t, p[1] + 0.12 * t, p[2] + 0.05 * t]
            else:
                t = 0.6 * fine(x, y) + 0.4 * coarse(x, y)
                p = [0.25 + 0.35 * t, 0.35 + 0.3 * t, 0.1 + 0.2 * t]
            if seed == 1 and (u - 0.25) ** 2 + (v - 0.3) ** 2 < 0.006:
                p = [0.95, 0.95, 0.9]
            if seed == 2:
                p = [0.7 + 0.15 * fine(x, y), 0.2 + 0.1 * coarse(x, y), 0.15]
            data += struct.pack('fff', *[min(max(c + rnd.gauss(0, 3 / 255), 0.0), 1.0)
                                         for c in p])
    return data


def new_image(data):
    image = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'scene', W, H, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, W, H), FMT, bytes(data))
    buf.flush()
    return image


def jpeg_copy(data, quality, name):
    """data saved as a JPEG with GIMP's exporter and opened again"""
    image = new_image(data)
    path = os.path.join(OUT, name)
    proc = Gimp.get_pdb().lookup_procedure('file-jpeg-export')
    cfg = proc.create_config()
    cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property('image', image)
    cfg.set_property('file', Gio.File.new_for_path(path))
    cfg.set_property('quality', quality / 100.0)
    cfg.set_property('sub-sampling', 'sub-sampling-2x2')
    proc.run(cfg)
    image.delete()
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))


photo = jpeg_copy(scene(1), 90, 'camera.jpg')
other = jpeg_copy(scene(2), 60, 'other.jpg')
layer = photo.get_layers()[0]
buf = layer.get_buffer()
# the paste: 176 x 128 pixels of the other picture, on the JPEG grid
paste = Gegl.Rectangle.new(272, 160, 176, 128)
buf.set(paste, FMT, other.get_layers()[0].get_buffer().get(paste, 1.0, FMT,
                                                           Gegl.AbyssPolicy.NONE))
# the clone: 64 x 48 pixels of the ground copied 150 pixels to the left
src = Gegl.Rectangle.new(200, 230, 64, 48)
buf.set(Gegl.Rectangle.new(50, 250, 64, 48), FMT,
        buf.get(src, 1.0, FMT, Gegl.AbyssPolicy.NONE))
buf.flush()
layer.update(0, 0, W, H)
other.delete()
layer.set_name('edited photo')
# (a copy is saved, so that the image stays the one opened from
# camera.jpg, for JPEG Info)
copy = photo.duplicate()
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, copy,
               Gio.File.new_for_path(os.path.join(OUT, 'edited.png')), None)
# the edited photo saved as a JPEG at 90 and opened again, as it would
# reach you: the image shown (JPEG Info reads its file)
export = photo.duplicate()
path = os.path.join(OUT, 'edited.jpg')
proc = Gimp.get_pdb().lookup_procedure('file-jpeg-export')
cfg = proc.create_config()
cfg.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
cfg.set_property('image', export)
cfg.set_property('file', Gio.File.new_for_path(path))
cfg.set_property('quality', 0.9)
cfg.set_property('sub-sampling', 'sub-sampling-2x2')
proc.run(cfg)
export.delete()
copy.delete()
photo.delete()
photo = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))
photo.get_layers()[0].set_name('edited photo')
photo.clean_all()
Gimp.Display.new(photo)
