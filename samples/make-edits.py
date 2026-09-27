#!/usr/bin/env python3
# Makes the edited sample images from three free camera photos that
# fetch-samples.py downloads (all CC0, see samples.json): each edit is
# one known change at a known place, so that you can check what the
# forensics tools show there. Every edited file comes with the same photo
# saved the same way without the edit (the "-original" file), and with a
# mask of where the edit is in images/masks/ (white: edited).
#
#   samples/make-edits.py     writes into samples/images (run fetch-samples.py first)
#
# Needs Python 3 with Pillow and numpy (the JPEG files are written by
# Pillow's libjpeg, 4:2:0 chroma subsampling, the standard IJG tables at
# the given quality, the same scale as GIMP's JPEG export). No EXIF is
# copied into the edited files. The output is the same on every run for
# the same Pillow version.
#
# What is done (sizes and places in pixels of the full image, x, y from
# the top left):
#
#   lake-original.jpg        ribnica-lake-camera.jpg (Nikon Coolpix L23, its
#                            own JPEG at quality 97 or higher, 4:2:2) saved again
#                            at quality 90
#   lake-splice-plane.jpg    the airliner of airplane-camera.jpg (Kodak Z812,
#                            grey sky) cut out with 8 pixels of its own sky
#                            around it, its colors shifted to the blue sky of
#                            the lake photo, saved at quality 60 (on the
#                            8 x 8 grid it will land on), pasted into the sky
#                            at x 1152, y 672 (a 368 x 160 piece), the whole
#                            saved at quality 90
#   lake-clone.jpg           a 400 x 400 piece of dwarf pines at x 2820,
#                            y 1668 copied to x 2052, y 2086 (over the snow
#                            patch at the bottom), with a 12 pixel soft edge
#                            as a clone brush leaves, saved at quality 90
#   lake-airbrush.jpg        the cloud at the right, an ellipse centred at
#                            x 3080, y 760 with radii 480 x 200, blurred
#                            (Gaussian, radius 4) with a 24 pixel soft edge,
#                            as an airbrush or a "smooth skin" tool does;
#                            saved at quality 90
#   lake-double-jpeg.jpg     the lake photo saved at quality 70, opened and
#                            saved again at quality 90: the whole image is
#                            double compressed, nothing else changed
#   eggs-original.jpg        eggs-camera.jpg (Canon EOS 250D) saved at 90
#   eggs-mirrored-egg.jpg    the large egg in the front, second from the
#                            left (its shell, found by its color, inside an
#                            ellipse centred at x 1505, y 1735 with radii
#                            380 x 440), replaced by its own mirror image
#                            (left and right swapped about x 1505) where the
#                            egg and its mirror image overlap, with a 6 pixel
#                            soft edge: its light now comes from the upper
#                            right, that of every other egg from the upper
#                            left; saved at quality 90
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import io
import json
import os
import sys

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
except ImportError as e:
    sys.exit('make-edits.py needs Pillow and numpy (%s)' % e)

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(HERE, 'images')
MASKS = os.path.join(IMAGES, 'masks')


def load(name):
    path = os.path.join(IMAGES, name)
    if not os.path.isfile(path):
        sys.exit('make-edits.py: %s is missing: run samples/fetch-samples.py first' % path)
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float64)


def jpeg(a, quality):
    """a JPEG round trip in memory, as the file would decode"""
    buf = io.BytesIO()
    Image.fromarray(to8(a)).save(buf, 'JPEG', quality=quality, subsampling=2)
    buf.seek(0)
    return np.asarray(Image.open(buf).convert('RGB'), dtype=np.float64)


def to8(a):
    return np.clip(np.rint(a), 0, 255).astype(np.uint8)


def save(a, name, quality=90):
    Image.fromarray(to8(a)).save(os.path.join(IMAGES, name), 'JPEG', quality=quality,
                                 subsampling=2)
    print('wrote', name)


def save_mask(m, name):
    os.makedirs(MASKS, exist_ok=True)
    Image.fromarray(to8(m * 255)).save(os.path.join(MASKS, name))


def soft(mask, feather):
    """a 0/1 mask with a soft edge of about `feather` pixels"""
    im = Image.fromarray(to8(mask * 255)).filter(ImageFilter.GaussianBlur(feather / 2.0))
    return np.asarray(im, dtype=np.float64) / 255.0


def ellipse(shape, cx, cy, rx, ry):
    h, w = shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    return (((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0).astype(np.float64)


def blend(base, top, alpha):
    return base * (1 - alpha[..., None]) + top * alpha[..., None]


regions = {}

# ------------------------------------------------------------------ lake

lake = load('ribnica-lake-camera.jpg')
H, W = lake.shape[:2]
save(lake, 'lake-original.jpg')

# the splice: the plane with a margin of its own sky
plane = load('airplane-camera.jpg')
px, py, pw, ph = 1280, 1040, 368, 160          # on the donor's 16 x 16 grid
piece = plane[py:py + ph, px:px + pw].copy()
sky = np.median(piece.reshape(-1, 3), axis=0)
diff = np.abs(piece - sky).max(axis=2)
diff = np.asarray(Image.fromarray(to8(diff)).filter(ImageFilter.GaussianBlur(2)),
                  dtype=np.float64)
body = diff > 18
# (the plane and its 8 pixel margin)
grown = Image.fromarray(to8(body * 255)).filter(ImageFilter.MaxFilter(17))
cut = np.asarray(grown, dtype=np.float64) / 255.0
tx, ty = 1152, 672                              # on the lake's 16 x 16 grid
target = lake[ty:ty + ph, tx:tx + pw]
# the donor's grey sky moved to the blue of the lake's sky there
sky_d = piece[cut < 0.5]
sky_t = target.reshape(-1, 3)
piece = (piece - sky_d.mean(axis=0)) * (sky_t.std(axis=0) / sky_d.std(axis=0)).clip(0.5, 1.5) \
    + sky_t.mean(axis=0)
piece = jpeg(piece, 60)
alpha = soft(cut, 3)
splice = lake.copy()
splice[ty:ty + ph, tx:tx + pw] = blend(target, piece, alpha)
save(splice, 'lake-splice-plane.jpg')
m = np.zeros((H, W))
m[ty:ty + ph, tx:tx + pw] = cut
save_mask(m, 'lake-splice-plane-mask.png')
regions['lake-splice-plane.jpg'] = {'mask': 'lake-splice-plane-mask.png',
                                    'piece': [tx, ty, pw, ph]}

# the copy-move
sx, sy, dx, dy, s = 2820, 1668, 2052, 2086, 400
clone = lake.copy()
a = np.zeros((s, s))
a[12:-12, 12:-12] = 1
a = soft(a, 12)
clone[dy:dy + s, dx:dx + s] = blend(lake[dy:dy + s, dx:dx + s], lake[sy:sy + s, sx:sx + s], a)
save(clone, 'lake-clone.jpg')
m = np.zeros((H, W))
m[dy:dy + s, dx:dx + s] = a
save_mask(m, 'lake-clone-mask.png')
regions['lake-clone.jpg'] = {'mask': 'lake-clone-mask.png', 'source': [sx, sy, s, s],
                             'copy': [dx, dy, s, s]}

# the airbrush
blurred = np.asarray(Image.fromarray(to8(lake)).filter(ImageFilter.GaussianBlur(4)),
                     dtype=np.float64)
e = soft(ellipse(lake.shape, 3080, 760, 480, 200), 24)
save(blend(lake, blurred, e), 'lake-airbrush.jpg')
save_mask(e, 'lake-airbrush-mask.png')
regions['lake-airbrush.jpg'] = {'mask': 'lake-airbrush-mask.png'}

# double compression
save(jpeg(lake, 70), 'lake-double-jpeg.jpg')

# ------------------------------------------------------------------ eggs

eggs = load('eggs-camera.jpg')
save(eggs, 'eggs-original.jpg')
cx, cy, rx, ry = 1505, 1735, 380, 440
# the egg: its orange shell (not the grey carton, not the dark gaps) inside
# an ellipse around it, which keeps out the eggs behind
shell = ((eggs[..., 0] - eggs[..., 2]) > 80) & (eggs[..., 0] > 125)
shell = Image.fromarray(to8(shell * 255)).filter(ImageFilter.MinFilter(9)) \
    .filter(ImageFilter.MaxFilter(9))
egg = (np.asarray(shell) > 128) * ellipse(eggs.shape, cx, cy, rx, ry)
box = (slice(cy - ry, cy + ry), slice(cx - rx, cx + rx))
# only where the egg and its mirror image overlap, with a 6 pixel soft edge
inside = egg[box] * egg[box][:, ::-1]
inside = np.asarray(Image.fromarray(to8(inside * 255)).filter(ImageFilter.MinFilter(7)),
                    dtype=np.float64) / 255.0
e = np.zeros(eggs.shape[:2])
e[box] = soft(inside, 6)
mirrored = eggs.copy()
mirrored[box] = blend(eggs[box], eggs[box][:, ::-1], e[box])
save(mirrored, 'eggs-mirrored-egg.jpg')
save_mask(e, 'eggs-mirrored-egg-mask.png')
regions['eggs-mirrored-egg.jpg'] = {'mask': 'eggs-mirrored-egg-mask.png',
                                    'egg': [cx, cy, rx, ry]}

with open(os.path.join(MASKS, 'regions.json'), 'w') as f:
    json.dump(regions, f, indent=1)
print('masks in', MASKS)
