# Runs inside GIMP on a Broadway display (tests/gui/start.sh): makes a
# synthetic "edited photo" and shows it, for looking at the forensics
# filters and the Forensics Workbench in GIMP's own dialogs.
#
# The photo: a generated scene saved as a quality 90 JPEG with GIMP's
# exporter and opened again (the camera's file); into it a region of
# another scene from a quality 60 JPEG (a paste from another picture),
# and a patch of the ground copied to another place (a clone). Written to
# FORENSICS_GUI_OUT/edited.png too. GIMP stays open.
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


def scene(seed):
    rnd = random.Random(seed)
    data = bytearray()
    for y in range(H):
        for x in range(W):
            u, v = x / (W - 1), y / (H - 1)
            if v < 0.45 + 0.05 * math.sin(6 * u + seed):
                p = [0.4 + 0.3 * v, 0.6 + 0.2 * v, 0.92 - 0.1 * v]
                t = 0.5 + 0.5 * math.sin(x / 23.0 + seed) * math.sin(y / 11.0)
                p = [p[0] + 0.12 * t, p[1] + 0.12 * t, p[2] + 0.05 * t]
            else:
                t = 0.5 + 0.25 * math.sin(x * 0.9 + seed) * math.sin(y * 0.7) + \
                    0.25 * math.sin(x * 0.13) * math.sin(y * 0.21 + seed)
                p = [0.25 + 0.35 * t, 0.35 + 0.3 * t, 0.1 + 0.2 * t]
            if seed == 1 and (u - 0.25) ** 2 + (v - 0.3) ** 2 < 0.006:
                p = [0.95, 0.95, 0.9]
            if seed == 2:
                p = [0.75 + 0.1 * math.sin(x * 0.5), 0.2 + 0.1 * math.sin(y * 0.3), 0.15]
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
# the paste: 96 x 80 pixels of the other picture, on the JPEG grid
paste = Gegl.Rectangle.new(304, 176, 96, 80)
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
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, photo,
               Gio.File.new_for_path(os.path.join(OUT, 'edited.png')), None)
photo.clean_all()
Gimp.Display.new(photo)
