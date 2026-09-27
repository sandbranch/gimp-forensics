/*
 * Resampling (interpolation) detection, a GEGL operation
 *
 * resampling.c
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
 * Periodic traces of interpolation, block by block. A region that was
 * enlarged, shrunk or rotated before it was pasted has pixels that are
 * weighted sums of their neighbours, with weights that repeat with the
 * resampling's period; how well a pixel is predicted from its neighbours
 * then repeats too, and the spectrum of that "probability map" has peaks.
 *
 * After A. C. Popescu and H. Farid, "Exposing Digital Forgeries by
 * Detecting Traces of Re-sampling", IEEE Transactions on Signal
 * Processing 53 (2), 2005 (the probability map and its spectrum), which
 * Sherloq's "Image Resampling" tool (Guido Bartoli and contributors,
 * github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/tampering/resampling.py) implements with the
 * EM estimate of the predictor (minutes per image, from random start
 * values); here, as in M. Kirchner, "Fast and reliable resampling
 * detection by spectral analysis of fixed linear predictor residue",
 * ACM MM&Sec 2008, with his fixed predictor: the residual e = s - (the
 * 3 x 3 kernel -1/4 1/2 -1/4 / 1/2 0 1/2 / -1/4 1/2 -1/4) * s on the 8 bit
 * luma and p = exp (-e^2) (his lambda = 1, sigma = 1, tau = 2). Each block
 * of b x b pixels: the magnitude of the 2D DFT of p minus its mean, under
 * a Hann window, as Sherloq's Fourier maps (Hanning option); the score is
 * its highest value over its median, away from the centre (radius above
 * 0.1, Sherloq's "Highpass 1") and away from the frequencies k / 8 of the
 * JPEG block grid, where every JPEG file has peaks. Unresampled noisy
 * content scores about 3.5; a region enlarged by 1.2 or 1.5 about 6 to 16
 * (tests/check-resampling.c). Shown: the score as gray (score / scale),
 * or each block's spectrum (centred, the masked frequencies black), as
 * the figures of Popescu and Farid.
 *
 * Reliable only on images never saved as JPEG: the rounding of a JPEG
 * save, even at quality 95, covers the fine differences p looks at.
 * Resampling to less than about 1.1 or down to 0.8 leaves peaks too weak
 * to see. Blocks that are not whole at the image's right and bottom edges
 * are black. Written for this operation. Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_resampling_mode)
  enum_value (FORENSICS_RESAMPLING_SCORE,    "score",    N_("Score"))
  enum_value (FORENSICS_RESAMPLING_SPECTRUM, "spectrum", N_("Spectrum per block"))
  enum_value (FORENSICS_RESAMPLING_PMAP,     "pmap",     N_("Probability map"))
enum_end (ForensicsResamplingMode)

property_enum (mode, _("Show"), ForensicsResamplingMode, forensics_resampling_mode,
               FORENSICS_RESAMPLING_SCORE)
  description (_("The score of each block (its spectrum's highest peak over "
                 "its median), each block's spectrum, or the probability "
                 "map itself"))

property_int (block_size, _("Block size"), 64)
  description (_("The side of the blocks, rounded down to a power of two "
                 "(larger blocks see weaker periodicity)"))
  value_range (32, 256)

property_double (scale, _("Scale"), 16.0)
  description (_("The score shown white"))
  value_range (1.0, 1000.0)
  ui_range (4.0, 64.0)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     resampling
#define GEGL_OP_C_SOURCE resampling.c

#include "gegl-op.h"
#include "forensics-common.h"
#include <stdlib.h>

static gint
block_side (GeglProperties *o)
{
  gint b = 32;

  while (b * 2 <= o->block_size && b < 256)
    b *= 2;
  return b;
}

static void
prepare (GeglOperation *operation)
{
  const Babl *format = fc_format (operation);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  return fc_bbox (operation);
}

static GeglRectangle
blocks_of (GeglOperation *operation, const GeglRectangle *r)
{
  GeglRectangle bbox = fc_bbox (operation);
  gint          b    = block_side (GEGL_PROPERTIES (operation));

  if (GEGL_PROPERTIES (operation)->mode == FORENSICS_RESAMPLING_PMAP)
    return *r;
  if (gegl_rectangle_is_infinite_plane (&bbox))
    {
      GeglRectangle g;
      gint64        x0 = (gint64) floor ((gdouble) r->x / b) * b;
      gint64        y0 = (gint64) floor ((gdouble) r->y / b) * b;

      g.x = (gint) x0;
      g.y = (gint) y0;
      g.width  = (gint) (((gint64) r->x + r->width - x0 + b - 1) / b * b);
      g.height = (gint) (((gint64) r->y + r->height - y0 + b - 1) / b * b);
      return g;
    }
  return fc_to_blocks (r, &bbox, b);
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle bbox = fc_bbox (operation);
  GeglRectangle r;

  (void) input_pad;
  r = blocks_of (operation, roi);
  r = fc_grow (&r, 1);
  if (! gegl_rectangle_is_infinite_plane (&bbox))
    gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  GeglRectangle bbox = fc_bbox (operation);
  GeglRectangle r;

  (void) input_pad;
  if (gegl_rectangle_is_infinite_plane (roi))
    return *roi;
  /* (2: the repeated edge pixels pass a change one pixel further) */
  r = fc_grow (roi, 2);
  r = blocks_of (operation, &r);
  gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

/* in place radix 2 FFT of n (a power of two) complex values, stride s */
static void
fft (gdouble *re, gdouble *im, gint n, gint s)
{
  gint i, j, len;

  for (i = 1, j = 0; i < n; i++)
    {
      gint bit = n >> 1;

      for (; j & bit; bit >>= 1)
        j ^= bit;
      j ^= bit;
      if (i < j)
        {
          gdouble t;

          t = re[i * s]; re[i * s] = re[j * s]; re[j * s] = t;
          t = im[i * s]; im[i * s] = im[j * s]; im[j * s] = t;
        }
    }
  for (len = 2; len <= n; len <<= 1)
    {
      gdouble ang = -2 * G_PI / len;
      gdouble wr = cos (ang), wi = sin (ang);

      for (i = 0; i < n; i += len)
        {
          gdouble cr = 1, ci = 0;

          for (j = 0; j < len / 2; j++)
            {
              gint    a = (i + j) * s, b = (i + j + len / 2) * s;
              gdouble xr = re[b] * cr - im[b] * ci;
              gdouble xi = re[b] * ci + im[b] * cr;
              gdouble t;

              re[b] = re[a] - xr;
              im[b] = im[a] - xi;
              re[a] += xr;
              im[a] += xi;
              t = cr * wr - ci * wi;
              ci = cr * wi + ci * wr;
              cr = t;
            }
        }
    }
}

static int
cmp_double (const void *a, const void *b)
{
  gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

  return x < y ? -1 : x > y;
}

/* whether frequency (u, v) of an n point DFT (indices 0 to n - 1) is
 * looked at: radius over 0.1 and not within 1.5 bins of a multiple of
 * 1/8 in both axes */
static gboolean
counted (gint u, gint v, gint n)
{
  gdouble fu = (u <= n / 2 ? u : u - n) / (gdouble) n;
  gdouble fv = (v <= n / 2 ? v : v - n) / (gdouble) n;
  gdouble du = fabs (fu * 8 - floor (fu * 8 + 0.5)) / 8 * n;
  gdouble dv = fabs (fv * 8 - floor (fv * 8 + 0.5)) / 8 * n;

  if (hypot (fu, fv) <= 0.1)
    return FALSE;
  return ! (du < 1.5 && dv < 1.5);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *format = fc_format (operation);
  GeglRectangle   bbox   = fc_bbox (operation);
  gboolean        inf    = gegl_rectangle_is_infinite_plane (&bbox);
  GeglRectangle   roi, blk, src, read, image;
  gfloat         *pix, *plane, *out;
  gdouble        *p, *re, *im, *mag, *vals, *win;
  gint            b = block_side (o), nbx, nby, bx, by, x, y, n;

  (void) level;
  if (inf)
    roi = *result;
  else if (! gegl_rectangle_intersect (&roi, result, &bbox))
    return TRUE;
  blk = blocks_of (operation, &roi);
  src = fc_grow (&blk, 1);
  if (inf)
    {
      read = src;
      image = src;
      pix = g_new (gfloat, (gsize) src.width * src.height * 4);
      gegl_buffer_get (input, &src, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    }
  else
    {
      image = bbox;
      pix = fc_read (input, format, &src, &bbox, &read);
    }
  plane = fc_plane (pix, &read, &src, &image, FC_LUMA, TRUE, FALSE);
  /* the probability map of blk */
  p = g_new (gdouble, (gsize) blk.width * blk.height);
  for (y = 0; y < blk.height; y++)
    for (x = 0; x < blk.width; x++)
      {
        const gfloat *c = plane + (gsize) (y + 1) * src.width + x + 1;
        gint          w = src.width;
        gdouble       pred = 0.5 * (c[-1] + c[1] + c[-w] + c[w]) -
                             0.25 * (c[-w - 1] + c[-w + 1] + c[w - 1] + c[w + 1]);
        gdouble       e = c[0] - pred;

        p[(gsize) y * blk.width + x] = exp (-e * e);
      }
  out = g_new0 (gfloat, (gsize) roi.width * roi.height * 4);
  if (o->mode == FORENSICS_RESAMPLING_PMAP)
    {
      for (y = 0; y < roi.height; y++)
        for (x = 0; x < roi.width; x++)
          {
            gfloat *q = out + ((gsize) y * roi.width + x) * 4;

            q[0] = q[1] = q[2] = (gfloat) p[(gsize) (roi.y + y - blk.y) * blk.width +
                                            roi.x + x - blk.x];
          }
      goto done;
    }
  nbx = blk.width / b;
  nby = blk.height / b;
  re = g_new (gdouble, (gsize) b * b);
  im = g_new (gdouble, (gsize) b * b);
  mag = g_new (gdouble, (gsize) b * b);
  vals = g_new (gdouble, (gsize) b * b);
  win = g_new (gdouble, b);
  for (x = 0; x < b; x++)
    win[x] = 0.5 - 0.5 * cos (2 * G_PI * x / (b - 1));   /* numpy's hanning */
  for (by = 0; by < nby; by++)
    for (bx = 0; bx < nbx; bx++)
      {
        gint    x0 = bx * b, y0 = by * b;
        gdouble mean = 0, peak = 0, med, score, top = 0;

        /* a block that the image does not fill: left black */
        if (! inf && (blk.x + x0 + b > bbox.x + bbox.width ||
                      blk.y + y0 + b > bbox.y + bbox.height))
          continue;
        for (y = 0; y < b; y++)
          for (x = 0; x < b; x++)
            mean += p[(gsize) (y0 + y) * blk.width + x0 + x];
        mean /= (gdouble) b * b;
        for (y = 0; y < b; y++)
          for (x = 0; x < b; x++)
            {
              re[y * b + x] = (p[(gsize) (y0 + y) * blk.width + x0 + x] - mean) * win[x] * win[y];
              im[y * b + x] = 0;
            }
        for (y = 0; y < b; y++)
          fft (re + y * b, im + y * b, b, 1);
        for (x = 0; x < b; x++)
          fft (re + x, im + x, b, b);
        n = 0;
        for (y = 0; y < b; y++)
          for (x = 0; x < b; x++)
            {
              gdouble m = hypot (re[y * b + x], im[y * b + x]);

              mag[y * b + x] = m;
              if (counted (x, y, b))
                {
                  vals[n++] = m;
                  peak = MAX (peak, m);
                }
            }
        qsort (vals, n, sizeof *vals, cmp_double);
        med = n ? vals[n / 2] : 0;
        score = med > 0 ? peak / med : 0;
        if (o->mode == FORENSICS_RESAMPLING_SPECTRUM)
          for (y = 0; y < b; y++)
            for (x = 0; x < b; x++)
              if (counted (x, y, b))
                top = MAX (top, log1p (mag[y * b + x]));
        for (y = 0; y < b; y++)
          for (x = 0; x < b; x++)
            {
              gint    ix = blk.x + x0 + x, iy = blk.y + y0 + y;
              gfloat *q;
              gfloat  v;

              if (ix < roi.x || iy < roi.y || ix >= roi.x + roi.width || iy >= roi.y + roi.height)
                continue;
              if (o->mode == FORENSICS_RESAMPLING_SPECTRUM)
                {
                  /* centred: the pixel (x, y) shows frequency (x - b/2, y - b/2) */
                  gint u = (x + b / 2) % b, v2 = (y + b / 2) % b;

                  v = counted (u, v2, b) && top > 0 ? (gfloat) (log1p (mag[v2 * b + u]) / top) : 0.0f;
                }
              else
                v = (gfloat) (score / o->scale);
              q = out + ((gsize) (iy - roi.y) * roi.width + ix - roi.x) * 4;
              q[0] = q[1] = q[2] = v;
            }
      }
  g_free (win);
  g_free (vals);
  g_free (mag);
  g_free (im);
  g_free (re);
done:
  fc_copy_alpha (pix, &read, &roi, out);
  gegl_buffer_set (output, &roi, 0, format, out, GEGL_AUTO_ROWSTRIDE);
  g_free (out);
  g_free (p);
  g_free (plane);
  g_free (pix);
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                   = prepare;
  operation_class->get_bounding_box          = get_bounding_box;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->opencl_support            = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:resampling",
    "title",           _("Resampling Detection"),
    "categories",      "forensics:analysis",
    "description",     _("Shows periodic traces of interpolation block by "
                         "block (Popescu and Farid 2005, Kirchner 2008): a "
                         "region enlarged or rotated before it was pasted "
                         "scores high. Reliable only on images never saved "
                         "as JPEG. An indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Resampling Detection..."),
    NULL);
}

#endif
