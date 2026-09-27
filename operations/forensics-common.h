/*
 * Pieces shared by the forensics operations added in the second round
 * (bit-plane, minmax, wavelet-noise, echo, median-detect, resampling,
 * contrast)
 *
 * forensics-common.h
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Included (not linked) by each operation, so that every module stays one
 * file for GEGL. The operations work on the image's encoded R'G'B'
 * values; most of the methods (after Sherloq, which reads 8 bit images
 * with OpenCV) look at them as 8 bit values, 0 to 255, rounded as babl
 * rounds. A plane is one channel of a rectangle: red, green, blue, the
 * luma of OpenCV's cvtColor (BGR2GRAY: 0.299 R + 0.587 G + 0.114 B in
 * fixed point, rounded), or the length of the R, G, B vector over the
 * square root of 3. Outside the image a plane holds copies of the edge
 * pixels, or their mirror image without the edge (OpenCV's default
 * border, BORDER_REFLECT_101).
 */

#ifndef FORENSICS_COMMON_H
#define FORENSICS_COMMON_H

#include <gegl.h>
#include <math.h>
#include <string.h>

enum
{
  FC_LUMA = 0,
  FC_RED,
  FC_GREEN,
  FC_BLUE,
  FC_NORM
};

static inline const Babl *
fc_format (GeglOperation *operation)
{
  return babl_format_with_space ("R'G'B'A float",
                                 gegl_operation_get_source_space (operation, "input"));
}

static inline GeglRectangle
fc_bbox (GeglOperation *operation)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");
  GeglRectangle        r  = { 0, 0, 0, 0 };

  if (in)
    r = *in;
  return r;
}

/* NaN as 0, infinities clamped */
static inline gfloat
fc_sane (gfloat v)
{
  if (v != v)
    return 0.0f;
  if (v > 1e6f)
    return 1.0f;
  if (v < -1e6f)
    return 0.0f;
  return v;
}

/* float to 8 bit as babl does it (x 255, rounded, clamped) */
static inline gint
fc_u8 (gfloat v)
{
  if (! (v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 255;
  return (gint) (v * 255.0f + 0.5f);
}

/* one channel of an R'G'B'A float pixel, 0 to 255: 8 bit values
 * (eight_bit) or not */
static inline gfloat
fc_channel (const gfloat *p,
            gint          channel,
            gboolean      eight_bit)
{
  if (eight_bit)
    {
      gint r = fc_u8 (p[0]), g = fc_u8 (p[1]), b = fc_u8 (p[2]);

      switch (channel)
        {
        case FC_RED:   return r;
        case FC_GREEN: return g;
        case FC_BLUE:  return b;
        case FC_NORM:
          return (gfloat) (gint) (sqrtf ((gfloat) (r * r + g * g + b * b) / 3.0f) + 0.5f);
        default:
          /* OpenCV's fixed point BGR2GRAY: (R 4899 + G 9617 + B 1868 + 8192) >> 14 */
          return (gfloat) ((r * 4899 + g * 9617 + b * 1868 + 8192) >> 14);
        }
    }
  else
    {
      gfloat r = fc_sane (p[0]) * 255.0f, g = fc_sane (p[1]) * 255.0f;
      gfloat b = fc_sane (p[2]) * 255.0f;

      switch (channel)
        {
        case FC_RED:   return r;
        case FC_GREEN: return g;
        case FC_BLUE:  return b;
        case FC_NORM:  return sqrtf ((r * r + g * g + b * b) / 3.0f);
        default:       return 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }
}

/* the index of v in [0, n): clamped (edge pixels repeated) or mirrored
 * without the edge (BORDER_REFLECT_101) */
static inline gint
fc_edge (gint     v,
         gint     n,
         gboolean reflect)
{
  if (n <= 1)
    return 0;
  if (! reflect)
    return CLAMP (v, 0, n - 1);
  while (v < 0 || v >= n)
    {
      if (v < 0)
        v = -v;
      if (v >= n)
        v = 2 * (n - 1) - v;
    }
  return v;
}

/* reads the part of want inside the image as R'G'B'A float; sets *got to
 * it (empty if want misses the image) */
static inline gfloat *
fc_read (GeglBuffer          *input,
         const Babl          *format,
         const GeglRectangle *want,
         const GeglRectangle *bbox,
         GeglRectangle       *got)
{
  gfloat *buf;

  if (! gegl_rectangle_intersect (got, want, bbox))
    return NULL;
  buf = g_new (gfloat, (gsize) got->width * got->height * 4);
  gegl_buffer_get (input, got, 1.0, format, buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  return buf;
}

/* a plane of the rectangle src (which may reach beyond the image) from
 * pixels read by fc_read (read covers the part of src in the image;
 * image is the image's rectangle, for the edges) */
static inline gfloat *
fc_plane (const gfloat        *pix,
          const GeglRectangle *read,
          const GeglRectangle *src,
          const GeglRectangle *image,
          gint                 channel,
          gboolean             eight_bit,
          gboolean             reflect)
{
  gfloat *plane = g_new (gfloat, (gsize) src->width * src->height);
  gint    x, y;

  for (y = 0; y < src->height; y++)
    {
      gint iy = image->y + fc_edge (src->y + y - image->y, image->height, reflect);

      iy = CLAMP (iy, read->y, read->y + read->height - 1) - read->y;
      for (x = 0; x < src->width; x++)
        {
          gint ix = image->x + fc_edge (src->x + x - image->x, image->width, reflect);

          ix = CLAMP (ix, read->x, read->x + read->width - 1) - read->x;
          plane[(gsize) y * src->width + x] =
            fc_channel (pix + ((gsize) iy * read->width + ix) * 4, channel, eight_bit);
        }
    }
  return plane;
}

/* the alpha of the pixels of roi (inside read), NaN as 0 */
static inline void
fc_copy_alpha (const gfloat        *pix,
               const GeglRectangle *read,
               const GeglRectangle *roi,
               gfloat              *out)
{
  gint x, y;

  for (y = 0; y < roi->height; y++)
    for (x = 0; x < roi->width; x++)
      {
        gfloat a = pix[((gsize) (roi->y + y - read->y) * read->width + roi->x + x - read->x) * 4 + 3];

        out[((gsize) y * roi->width + x) * 4 + 3] = a == a ? a : 0.0f;
      }
}

/* r grown to whole blocks of size b on the grid that starts at the
 * image's corner, and clipped to the image */
static inline GeglRectangle
fc_to_blocks (const GeglRectangle *r,
              const GeglRectangle *image,
              gint                 b)
{
  GeglRectangle g;
  gint64        x0 = image->x + ((gint64) r->x - image->x) / b * b;
  gint64        y0 = image->y + ((gint64) r->y - image->y) / b * b;
  gint64        x1 = (gint64) r->x + r->width, y1 = (gint64) r->y + r->height;

  if (r->x < image->x)
    x0 = image->x;
  if (r->y < image->y)
    y0 = image->y;
  x1 = image->x + ((x1 - image->x + b - 1) / b) * b;
  y1 = image->y + ((y1 - image->y + b - 1) / b) * b;
  g.x = (gint) x0;
  g.y = (gint) y0;
  g.width  = (gint) MAX (x1 - x0, 0);
  g.height = (gint) MAX (y1 - y0, 0);
  gegl_rectangle_intersect (&g, &g, image);
  return g;
}

/* a rectangle grown by m on each side */
static inline GeglRectangle
fc_grow (const GeglRectangle *r,
         gint                 m)
{
  GeglRectangle g = { r->x - m, r->y - m, r->width + 2 * m, r->height + 2 * m };

  return g;
}

#endif
