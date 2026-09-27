/*
 * JPEG helpers of the check programs (tests/check-error-level.c,
 * tests/check-jpeg-ghost.c)
 *
 * check-jpeg.h
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 8 bit images, JPEG files made with the operations' own round trip
 * (forensics-jpeg.h), on the image's grid or shifted, whole images on
 * their padded canvas, and pasting a region.
 */

#ifndef CHECK_JPEG_H
#define CHECK_JPEG_H

#include "check-common.h"
#include "forensics-jpeg.h"

/* 8 bit R'G'B' of an R'G'B'A float image, as the operation reads it */
static inline guint8 *
to_u8 (const gfloat *p, gint w, gint h)
{
  guint8 *q = g_new (guint8, (gsize) w * h * 3);
  gsize   i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      q[3 * i + 0] = fx_to_u8 (p[4 * i + 0]);
      q[3 * i + 1] = fx_to_u8 (p[4 * i + 1]);
      q[3 * i + 2] = fx_to_u8 (p[4 * i + 2]);
    }
  return q;
}

static inline gfloat *
from_u8 (const guint8 *q, gint w, gint h)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  gsize   i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      p[4 * i + 0] = q[3 * i + 0] / 255.0f;
      p[4 * i + 1] = q[3 * i + 1] / 255.0f;
      p[4 * i + 2] = q[3 * i + 2] / 255.0f;
      p[4 * i + 3] = 1.0f;
    }
  return p;
}

/* a JPEG round trip of a whole 8 bit image (a file saved and opened) */
static inline guint8 *
jpeg_file (const guint8 *rgb, gint w, gint h, gint quality, gint chroma)
{
  guint8 *out = g_new (guint8, (gsize) w * h * 3);

  if (! fx_jpeg_round_trip (rgb, w, h, quality, chroma, out))
    g_error ("libjpeg failed");
  return out;
}

/* a region taken from the scene src, placed into the JPEG image at r */
static inline void
paste (guint8 *dst, const guint8 *src, gint w, const GeglRectangle *r)
{
  gint y;

  for (y = r->y; y < r->y + r->height; y++)
    memcpy (dst + ((gsize) y * w + r->x) * 3, src + ((gsize) y * w + r->x) * 3,
            (gsize) r->width * 3);
}

/* src saved as a JPEG with its blocks shifted by (dx, dy): a copy made
 * from a crop, or pasted at another position */
static inline guint8 *
jpeg_shifted (const guint8 *src, gint w, gint h, gint dx, gint dy, gint q)
{
  gint    cw = w + dx, ch = h + dy, x, y;
  guint8 *c = g_new (guint8, (gsize) cw * ch * 3), *j, *out;

  for (y = 0; y < ch; y++)
    for (x = 0; x < cw; x++)
      memcpy (c + ((gsize) y * cw + x) * 3,
              src + ((gsize) MAX (y - dy, 0) * w + MAX (x - dx, 0)) * 3, 3);
  j   = jpeg_file (c, cw, ch, q, FX_CHROMA_420);
  out = g_new (guint8, (gsize) w * h * 3);
  for (y = 0; y < h; y++)
    memcpy (out + (gsize) y * w * 3, j + ((gsize) (y + dy) * cw + dx) * 3,
            (gsize) w * 3);
  g_free (c);
  g_free (j);
  return out;
}

/* the image (8 bit R'G'B') on its canvas for the grid offset (edge
 * pixels copied into the partial blocks left and above), saved as a JPEG
 * and decoded, cut back to the image: what the operations compare with */
static inline guint8 *
canvas_round_trip (const guint8 *u8, gint w, gint h, gint quality, gint chroma,
                   gint ox, gint oy)
{
  gint    px = (16 - ox % 16) % 16, py = (16 - oy % 16) % 16;
  gint    cw = w + px, ch = h + py, x, y;
  guint8 *canvas = g_new (guint8, (gsize) cw * ch * 3), *copy;
  guint8 *out = g_new (guint8, (gsize) w * h * 3);

  for (y = 0; y < ch; y++)
    for (x = 0; x < cw; x++)
      memcpy (canvas + ((gsize) y * cw + x) * 3,
              u8 + ((gsize) MAX (y - py, 0) * w + MAX (x - px, 0)) * 3, 3);
  copy = jpeg_file (canvas, cw, ch, quality, chroma);
  for (y = 0; y < h; y++)
    memcpy (out + (gsize) y * w * 3, copy + ((gsize) (y + py) * cw + px) * 3,
            (gsize) w * 3);
  g_free (canvas);
  g_free (copy);
  return out;
}

#endif
