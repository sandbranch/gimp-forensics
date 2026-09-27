/*
 * Wavelet noise level map (noise inconsistencies), a GEGL operation
 *
 * wavelet-noise.c
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
 * The noise level of each block of the image, estimated from the finest
 * diagonal wavelet details, after B. Mahdian and S. Saic, "Using noise
 * inconsistencies for blind image forensics", Image and Vision Computing
 * 27 (10), 2009, as the "Wavelet Blocking" tool of Sherloq (Guido Bartoli
 * and contributors, github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/noise/noise_estimmation.py) does it: the gray
 * image, one level of the Daubechies 8 wavelet transform (pywt.dwt2
 * with 'db8', symmetric extension), the HH subband cut into blocks of b x
 * b coefficients, and in each block the robust estimate of the noise's
 * standard deviation, median (|HH|) / 0.6745 (Donoho's estimator). Like
 * Sherloq, the paper's merging of blocks into regions is left out. A
 * picture from one camera at one ISO has about one noise level; a region
 * pasted from another, or blurred, denoised or painted, can have another.
 * Texture and edges raise the estimate too (less than the mean would):
 * compare similar content. Written for this operation after the paper and
 * Sherloq's code.
 *
 * A block of b x b coefficients covers 2b x 2b pixels, on a grid from the
 * image's top left corner; the blocks at the right and bottom edges are
 * partial (Sherloq drops those coefficients). Shown as gray: the noise
 * level in 8 bit levels times the gain / 255, or normalised as Sherloq
 * shows it (the lowest block black, the highest white; this needs the
 * whole image). Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_int (block_size, _("Block size"), 8)
  description (_("The side of a block in wavelet coefficients (a block covers "
                 "twice as many pixels)"))
  value_range (2, 64)

property_double (gain, _("Gain"), 20.0)
  description (_("How bright a noise level is: gray = level (in 8 bit levels) "
                 "x gain / 255"))
  value_range (0.0, 1000.0)
  ui_range (1.0, 100.0)
  ui_gamma (2.0)

property_boolean (normalize, _("Normalize"), FALSE)
  description (_("Show the lowest level black and the highest white, as "
                 "Sherloq does, instead of by the gain (the analysis then "
                 "needs the whole image)"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     wavelet_noise
#define GEGL_OP_C_SOURCE wavelet-noise.c

#include "gegl-op.h"
#include "forensics-common.h"
#include <stdlib.h>

/* the decomposition low-pass filter of Daubechies 8 (16 taps, as pywt's
 * 'db8' dec_lo); the high-pass one is h[k] = (-1)^k lo[15 - k] */
static const gdouble DB8_LO[16] = {
  -0.00011747678400228192, 0.0006754494059985568, -0.0003917403729959771,
  -0.00487035299301066, 0.008746094047015655, 0.013981027917015516,
  -0.04408825393106472, -0.01736930100202211, 0.128747426620186,
  0.00047248457399797254, -0.2840155429624281, -0.015829105256023893,
  0.5853546836548691, 0.6756307362980128, 0.3128715909144659,
  0.05441584224308161
};

#define TAPS  16
#define SHIFT 7    /* coefficient i uses pixels 2 i - SHIFT to 2 i - SHIFT + 15 */

static gboolean
whole_image (GeglOperation *operation)
{
  GeglRectangle bbox = fc_bbox (operation);

  return GEGL_PROPERTIES (operation)->normalize && ! gegl_rectangle_is_infinite_plane (&bbox);
}

static gboolean
passes_through (GeglOperation *operation)
{
  GeglRectangle bbox = fc_bbox (operation);

  return GEGL_PROPERTIES (operation)->normalize && gegl_rectangle_is_infinite_plane (&bbox);
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

/* the pixels of the blocks that cover r (2b on the image's grid) */
static GeglRectangle
blocks_of (GeglOperation *operation, const GeglRectangle *r)
{
  GeglRectangle bbox = fc_bbox (operation);
  gint          b2   = 2 * GEGL_PROPERTIES (operation)->block_size;

  if (gegl_rectangle_is_infinite_plane (&bbox))
    {
      GeglRectangle g = *r;
      gint64        x0 = (gint64) floor ((gdouble) r->x / b2) * b2;
      gint64        y0 = (gint64) floor ((gdouble) r->y / b2) * b2;

      g.width  = (gint) (((gint64) r->x + r->width - x0 + b2 - 1) / b2 * b2);
      g.height = (gint) (((gint64) r->y + r->height - y0 + b2 - 1) / b2 * b2);
      g.x = (gint) x0;
      g.y = (gint) y0;
      return g;
    }
  return fc_to_blocks (r, &bbox, b2);
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle bbox = fc_bbox (operation);
  GeglRectangle r;

  (void) input_pad;
  if (passes_through (operation))
    return *roi;
  if (whole_image (operation))
    return bbox;
  r = blocks_of (operation, roi);
  r.x -= SHIFT;
  r.y -= SHIFT;
  r.width += TAPS;
  r.height += TAPS;
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
  GeglRectangle r    = *roi;

  (void) input_pad;
  if (gegl_rectangle_is_infinite_plane (roi) || passes_through (operation))
    return *roi;
  if (whole_image (operation))
    return bbox;
  /* the coefficients that use a pixel reach 8 pixels to each side (and
   * the symmetric extension mirrors pixels near the edges into them) */
  r.x -= TAPS;
  r.y -= TAPS;
  r.width += 2 * TAPS;
  r.height += 2 * TAPS;
  r = blocks_of (operation, &r);
  gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  return whole_image (operation) ? fc_bbox (operation) : *roi;
}

/* pywt's 'symmetric' extension: ... x1 x0 | x0 x1 ... x(n-1) | x(n-1) x(n-2) ... */
static inline gint
symmetric (gint v, gint n)
{
  if (n <= 1)
    return 0;
  for (;;)
    {
      if (v < 0)
        v = -v - 1;
      else if (v >= n)
        v = 2 * n - 1 - v;
      else
        return v;
    }
}

static int
cmp_float (const void *a, const void *b)
{
  gfloat x = *(const gfloat *) a, y = *(const gfloat *) b;

  return x < y ? -1 : x > y;
}

typedef struct
{
  GeglBuffer   *input;
  const Babl   *format;
  GeglRectangle bbox;      /* the image (or the plane, if infinite) */
  gboolean      infinite;
  GeglRectangle blocks;    /* in pixels, on the block grid */
  gint          b;
  gint          nbx, nby;
  gfloat       *sigma;     /* nbx x nby */
} Job;

/* the noise levels of a row of blocks */
static void
block_row (Job *job, gint by)
{
  gint          b = job->b, b2 = 2 * b;
  gint          y0 = job->blocks.y + by * b2;              /* pixel rows */
  gint          ny = MIN (b2, job->bbox.y + job->bbox.height - y0);
  gint          ncy = job->infinite ? b : (ny + 1) / 2;    /* coefficient rows */
  gint          ncx_all = job->infinite ? job->nbx * b
                                        : (MIN (job->blocks.width,
                                                job->bbox.x + job->bbox.width -
                                                job->blocks.x) + 1) / 2;
  GeglRectangle src = { job->blocks.x - SHIFT, y0 - SHIFT, 2 * ncx_all + TAPS, 2 * ncy + TAPS };
  GeglRectangle want, read;
  gfloat       *pix, *plane, *rows, *hh, *vals;
  gdouble       hi[TAPS];
  gint          k, x, y, i, j, bx;

  if (ny <= 0 || ncx_all <= 0)
    return;
  for (k = 0; k < TAPS; k++)
    hi[k] = (k % 2 ? -1.0 : 1.0) * DB8_LO[TAPS - 1 - k];
  /* the pixels, with the symmetric extension at the image's edges */
  if (job->infinite)
    {
      want = src;
      pix = g_new (gfloat, (gsize) want.width * want.height * 4);
      gegl_buffer_get (job->input, &want, 1.0, job->format, pix, GEGL_AUTO_ROWSTRIDE,
                       GEGL_ABYSS_NONE);
      read = want;
      plane = fc_plane (pix, &read, &src, &want, FC_LUMA, TRUE, FALSE);
    }
  else
    {
      GeglRectangle all;
      gint          yy, xx, lo_x = G_MAXINT, hi_x = G_MININT, lo_y = G_MAXINT, hi_y = G_MININT;

      /* the pixels the mirrored indices land on */
      for (yy = 0; yy < src.height; yy++)
        {
          gint v = job->bbox.y + symmetric (src.y + yy - job->bbox.y, job->bbox.height);

          lo_y = MIN (lo_y, v);
          hi_y = MAX (hi_y, v);
        }
      for (xx = 0; xx < src.width; xx++)
        {
          gint v = job->bbox.x + symmetric (src.x + xx - job->bbox.x, job->bbox.width);

          lo_x = MIN (lo_x, v);
          hi_x = MAX (hi_x, v);
        }
      all.x = lo_x;
      all.y = lo_y;
      all.width = hi_x - lo_x + 1;
      all.height = hi_y - lo_y + 1;
      pix = fc_read (job->input, job->format, &all, &job->bbox, &read);
      plane = g_new (gfloat, (gsize) src.width * src.height);
      for (yy = 0; yy < src.height; yy++)
        {
          gint iy = job->bbox.y + symmetric (src.y + yy - job->bbox.y, job->bbox.height) - read.y;

          for (xx = 0; xx < src.width; xx++)
            {
              gint ix = job->bbox.x + symmetric (src.x + xx - job->bbox.x, job->bbox.width) - read.x;

              plane[(gsize) yy * src.width + xx] =
                fc_channel (pix + ((gsize) iy * read.width + ix) * 4, FC_LUMA, TRUE);
            }
        }
    }

  /* high pass along rows (every second column), then along columns */
  rows = g_new (gfloat, (gsize) ncx_all * src.height);
  for (y = 0; y < src.height; y++)
    for (x = 0; x < ncx_all; x++)
      {
        const gfloat *p = plane + (gsize) y * src.width + 2 * x;
        gdouble       s = 0;

        for (k = 0; k < TAPS; k++)
          s += hi[k] * p[k];
        rows[(gsize) y * ncx_all + x] = (gfloat) s;
      }
  hh = g_new (gfloat, (gsize) ncx_all * ncy);
  for (y = 0; y < ncy; y++)
    for (x = 0; x < ncx_all; x++)
      {
        gdouble s = 0;

        for (k = 0; k < TAPS; k++)
          s += hi[k] * rows[(gsize) (2 * y + k) * ncx_all + x];
        hh[(gsize) y * ncx_all + x] = (gfloat) fabs (s);
      }

  vals = g_new (gfloat, (gsize) b * b);
  for (bx = 0; bx < job->nbx; bx++)
    {
      gint n = 0;

      for (j = 0; j < MIN (b, ncy); j++)
        for (i = bx * b; i < MIN (bx * b + b, ncx_all); i++)
          vals[n++] = hh[(gsize) j * ncx_all + i];
      if (n == 0)
        {
          job->sigma[(gsize) by * job->nbx + bx] = 0.0f;
          continue;
        }
      qsort (vals, n, sizeof *vals, cmp_float);
      /* the median (the mean of the two middle ones for an even count, as
       * numpy's median) */
      job->sigma[(gsize) by * job->nbx + bx] =
        (n % 2 ? vals[n / 2] : 0.5f * (vals[n / 2 - 1] + vals[n / 2])) / 0.6745f;
    }
  g_free (vals);
  g_free (hh);
  g_free (rows);
  g_free (plane);
  g_free (pix);
}

static void
do_rows (gsize first, gsize n, gpointer data)
{
  gsize i;

  for (i = first; i < first + n; i++)
    block_row (data, (gint) i);
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
  GeglRectangle   roi, read;
  Job             job;
  gfloat         *out, *pix, lo = 0, scale;
  gint            x, y;

  (void) level;
  if (passes_through (operation))
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }
  memset (&job, 0, sizeof job);
  job.infinite = gegl_rectangle_is_infinite_plane (&bbox);
  if (job.infinite)
    roi = *result;
  else if (! gegl_rectangle_intersect (&roi, result, &bbox))
    return TRUE;
  job.input  = input;
  job.format = format;
  job.bbox   = job.infinite ? blocks_of (operation, &roi) : bbox;
  job.b      = CLAMP (o->block_size, 2, 64);
  job.blocks = blocks_of (operation, o->normalize ? &bbox : &roi);
  job.nbx    = (job.blocks.width + 2 * job.b - 1) / (2 * job.b);
  job.nby    = (job.blocks.height + 2 * job.b - 1) / (2 * job.b);
  job.sigma  = g_new0 (gfloat, (gsize) job.nbx * job.nby);
  gegl_parallel_distribute_range (job.nby, 1.0 / MAX (job.nbx, 1), do_rows, &job);

  scale = (gfloat) (o->gain / 255.0);
  if (o->normalize)
    {
      gfloat hi = -G_MAXFLOAT;
      gsize  i;

      lo = G_MAXFLOAT;
      for (i = 0; i < (gsize) job.nbx * job.nby; i++)
        {
          lo = MIN (lo, job.sigma[i]);
          hi = MAX (hi, job.sigma[i]);
        }
      scale = hi > lo ? 1.0f / (hi - lo) : 0.0f;
    }
  out = g_new (gfloat, (gsize) roi.width * roi.height * 4);
  for (y = 0; y < roi.height; y++)
    for (x = 0; x < roi.width; x++)
      {
        gint    bx = (roi.x + x - job.blocks.x) / (2 * job.b);
        gint    by = (roi.y + y - job.blocks.y) / (2 * job.b);
        gfloat  v = (job.sigma[(gsize) by * job.nbx + bx] - lo) * scale;
        gfloat *p = out + ((gsize) y * roi.width + x) * 4;

        p[0] = p[1] = p[2] = v;
        p[3] = 1.0f;
      }
  if (job.infinite)
    {
      read = roi;
      pix = g_new (gfloat, (gsize) roi.width * roi.height * 4);
      gegl_buffer_get (input, &roi, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    }
  else
    pix = fc_read (input, format, &roi, &bbox, &read);
  fc_copy_alpha (pix, &read, &roi, out);
  gegl_buffer_set (output, &roi, 0, format, out, GEGL_AUTO_ROWSTRIDE);
  g_free (pix);
  g_free (out);
  g_free (job.sigma);
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
  operation_class->get_cached_region         = get_cached_region;
  operation_class->opencl_support            = FALSE;
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:wavelet-noise",
    "title",           _("Wavelet Noise Map"),
    "categories",      "forensics:analysis",
    "description",     _("Shows the noise level of each block, from the finest "
                         "wavelet details (Mahdian and Saic 2009, as Sherloq's "
                         "Wavelet Blocking): a region pasted from another "
                         "picture, or blurred or denoised, can have another "
                         "level. Texture raises it too. An indicator, not "
                         "proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Wavelet Noise Map..."),
    NULL);
}

#endif
