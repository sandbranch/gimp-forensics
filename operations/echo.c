/*
 * Echo edge filter, a GEGL operation
 *
 * echo.c
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
 * The size of the Laplacian (the second derivative) of each channel,
 * strongly amplified, after the "Echo Edge Filter" of Sherloq (Guido
 * Bartoli, github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/detail/echo.py): |cv.Laplacian (channel, CV_64F,
 * ksize = 2 radius + 1)|, normalised per channel to 0 to 255 over the
 * image, then a contrast curve that maps 0 to 255 - contrast x 255 onto 0
 * to 255 (create_lut (0, contrast)). Fine detail, edges and noise light
 * up; a region that is out of focus while its surroundings are sharp,
 * or that was blurred, smoothed or painted, stays dark ("to reveal
 * artificial out-of-focus regions", Sherloq's description). Written for
 * this operation after Sherloq's code and OpenCV's documented kernels.
 *
 * OpenCV's Laplacian: for ksize 3 the kernel 2 0 2 / 0 -8 0 / 2 0 2; for
 * larger ones the sum of the separable second derivatives, the
 * derivative kernel (binomial of ksize - 2 convolved with 1 -2 1) along one
 * axis and the binomial smoothing of ksize along the other; the image
 * mirrored at its edges without repeating them (BORDER_REFLECT_101). The
 * channels are the 8 bit values, as Sherloq reads them.
 *
 * Normalised (as Sherloq, the default) the analysis needs the whole
 * image; without it the size is shown times the gain, in 8 bit levels.
 * Grayscale: the gray of the result (OpenCV's weights), as Sherloq's
 * option. Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_int (radius, _("Radius"), 2)
  description (_("The Laplacian's kernel is 2 x radius + 1 pixels wide"))
  value_range (1, 15)

property_double (contrast, _("Contrast"), 85.0)
  description (_("How strongly the result is brightened, in percent (the "
                 "top contrast percent of the range becomes white)"))
  value_range (0.0, 100.0)

property_boolean (normalize, _("Normalize"), TRUE)
  description (_("Scale each channel from its lowest to its highest value "
                 "over the image, as Sherloq does (needs the whole image); "
                 "without it, by the gain"))

property_double (gain, _("Gain"), 1.0)
  description (_("Without normalizing: gray = Laplacian (8 bit levels) x "
                 "gain / 255, before the contrast"))
  value_range (0.0, 1000.0)
  ui_range (0.01, 20.0)
  ui_gamma (2.0)

property_boolean (grayscale, _("Grayscale"), FALSE)
  description (_("Show the gray of the result instead of its colors"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     echo
#define GEGL_OP_C_SOURCE echo.c

#include "gegl-op.h"
#include "forensics-common.h"

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

static gint
reach (GeglOperation *operation)
{
  return CLAMP (GEGL_PROPERTIES (operation)->radius, 1, 15);
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
  r = fc_grow (roi, reach (operation));
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
  if (gegl_rectangle_is_infinite_plane (roi) || passes_through (operation))
    return *roi;
  if (whole_image (operation))
    return bbox;
  /* (twice the reach: the mirroring at an edge reflects a change back) */
  r = fc_grow (roi, 2 * reach (operation));
  gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  return whole_image (operation) ? fc_bbox (operation) : *roi;
}

/* OpenCV's getKernel (): smoothing (order 0) or derivative kernels of
 * size k */
static void
cv_kernel (gint order, gint k, gdouble *ker)
{
  gint i, j;

  for (i = 0; i <= k; i++)
    ker[i] = 0;
  ker[0] = 1;
  for (i = 0; i < k - order - 1; i++)
    {
      gdouble old = ker[0];

      for (j = 1; j <= k; j++)
        {
          gdouble nv = ker[j] + ker[j - 1];

          ker[j - 1] = old;
          old = nv;
        }
    }
  for (i = 0; i < order; i++)
    {
      gdouble old = -ker[0];

      for (j = 1; j <= k; j++)
        {
          gdouble nv = ker[j - 1] - ker[j];

          ker[j - 1] = old;
          old = nv;
        }
    }
}

typedef struct
{
  GeglBuffer   *input;
  const Babl   *format;
  GeglRectangle bbox;
  gboolean      infinite;
  GeglRectangle roi;
  gint          r, band_rows, n_bands;
  gdouble       kd[32], ks[32];
  gfloat       *lap;             /* |L| per channel, 3 per pixel of roi */
} Job;

static void
laplacian_band (Job *job, gint band)
{
  GeglRectangle need = job->roi, src, read;
  gfloat       *pix, *plane[3], *t;
  gint          r = job->r, k = 2 * r + 1, c, x, y, i;

  need.y = job->roi.y + band * job->band_rows;
  need.height = MIN (job->band_rows, job->roi.y + job->roi.height - need.y);
  if (need.height <= 0)
    return;
  src = fc_grow (&need, r);
  if (job->infinite)
    {
      read = src;
      pix = g_new (gfloat, (gsize) src.width * src.height * 4);
      gegl_buffer_get (job->input, &src, 1.0, job->format, pix, GEGL_AUTO_ROWSTRIDE,
                       GEGL_ABYSS_NONE);
    }
  else
    {
      /* the mirrored pixels lie within r of src's part in the image */
      GeglRectangle want = fc_grow (&src, r);

      pix = fc_read (job->input, job->format, &want, &job->bbox, &read);
    }
  for (c = 0; c < 3; c++)
    plane[c] = fc_plane (pix, &read, &src, job->infinite ? &src : &job->bbox,
                         FC_RED + c, TRUE, TRUE);
  t = g_new (gfloat, (gsize) need.width * src.height);
  for (c = 0; c < 3; c++)
    {
      const gfloat *p = plane[c];
      gfloat       *acc = g_new0 (gfloat, (gsize) need.width * need.height);

      if (k == 3)
        {
          for (y = 0; y < need.height; y++)
            for (x = 0; x < need.width; x++)
              {
                const gfloat *q = p + (gsize) (y + 1) * src.width + x + 1;

                acc[(gsize) y * need.width + x] =
                  2 * (q[-src.width - 1] + q[-src.width + 1] + q[src.width - 1] +
                       q[src.width + 1]) - 8 * q[0];
              }
        }
      else
        {
          gint pass;

          /* d2x: derivative along x, smoothing along y; then d2y */
          for (pass = 0; pass < 2; pass++)
            {
              const gdouble *kx = pass ? job->ks : job->kd;
              const gdouble *ky = pass ? job->kd : job->ks;

              for (y = 0; y < src.height; y++)
                for (x = 0; x < need.width; x++)
                  {
                    gdouble s = 0;

                    for (i = 0; i < k; i++)
                      s += kx[i] * p[(gsize) y * src.width + x + i];
                    t[(gsize) y * need.width + x] = (gfloat) s;
                  }
              for (y = 0; y < need.height; y++)
                for (x = 0; x < need.width; x++)
                  {
                    gdouble s = 0;

                    for (i = 0; i < k; i++)
                      s += ky[i] * t[(gsize) (y + i) * need.width + x];
                    acc[(gsize) y * need.width + x] += (gfloat) s;
                  }
            }
        }
      for (y = 0; y < need.height; y++)
        for (x = 0; x < need.width; x++)
          job->lap[((gsize) (need.y - job->roi.y + y) * job->roi.width + x) * 3 + c] =
            fabsf (acc[(gsize) y * need.width + x]);
      g_free (acc);
    }
  g_free (t);
  for (c = 0; c < 3; c++)
    g_free (plane[c]);
  g_free (pix);
}

static void
do_bands (gsize first, gsize n, gpointer data)
{
  gsize i;

  for (i = first; i < first + n; i++)
    laplacian_band (data, (gint) i);
}

static void
run (Job *job)
{
  gint n_threads;

  g_object_get (gegl_config (), "threads", &n_threads, NULL);
  job->band_rows = MAX ((job->roi.height + 2 * n_threads - 1) / (2 * n_threads), 8);
  job->n_bands = (job->roi.height + job->band_rows - 1) / job->band_rows;
  gegl_parallel_distribute_range (job->n_bands, 1.0 / MAX (job->roi.width, 1), do_bands, job);
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
  gdouble         lo[3] = { 0, 0, 0 }, hi[3] = { 1, 1, 1 };
  gfloat         *out, *pix, *lap;
  gdouble         lut_scale;
  gint            high, c, x, y, k;

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
  job.input = input;
  job.format = format;
  job.bbox = bbox;
  job.r = CLAMP (o->radius, 1, 15);
  k = 2 * job.r + 1;
  cv_kernel (2, k, job.kd);
  cv_kernel (0, k, job.ks);
  /* normalised: |L| over the whole image, for its range per channel */
  job.roi = o->normalize ? bbox : roi;
  job.lap = g_new (gfloat, (gsize) job.roi.width * job.roi.height * 3);
  run (&job);
  if (o->normalize)
    {
      gsize i, n = (gsize) job.roi.width * job.roi.height;

      for (c = 0; c < 3; c++)
        {
          lo[c] = G_MAXDOUBLE;
          hi[c] = -G_MAXDOUBLE;
        }
      for (i = 0; i < n; i++)
        for (c = 0; c < 3; c++)
          {
            gdouble v = job.lap[i * 3 + c];

            lo[c] = MIN (lo[c], v);
            hi[c] = MAX (hi[c], v);
          }
    }
  /* create_lut (0, contrast): 0 to 255 - high onto 0 to 255 */
  high = (gint) (o->contrast / 100.0 * 255.0);
  lut_scale = high >= 255 ? -1.0 : 255.0 / (255.0 - high);
  out = g_new (gfloat, (gsize) roi.width * roi.height * 4);
  lap = job.lap;
  for (y = 0; y < roi.height; y++)
    for (x = 0; x < roi.width; x++)
      {
        gsize   j = (gsize) (roi.y + y - job.roi.y) * job.roi.width + roi.x + x - job.roi.x;
        gfloat *p = out + ((gsize) y * roi.width + x) * 4;
        gint    v8[3];

        for (c = 0; c < 3; c++)
          {
            gdouble v = lap[j * 3 + c], n;

            if (o->normalize)
              /* cv.normalize (..., 0, 255, NORM_MINMAX, CV_8UC1): rounded */
              n = hi[c] > lo[c] ? floor ((v - lo[c]) * 255.0 / (hi[c] - lo[c]) + 0.5) : 0;
            else
              n = v * o->gain;
            /* cv.LUT: the curve's value, truncated to 8 bit */
            if (lut_scale < 0)
              v8[c] = 255;
            else
              v8[c] = (gint) CLAMP (floor (MIN (n, 255.0) * lut_scale), 0, 255);
          }
        if (o->grayscale)
          {
            gint g = (v8[0] * 4899 + v8[1] * 9617 + v8[2] * 1868 + 8192) >> 14;

            p[0] = p[1] = p[2] = g / 255.0f;
          }
        else
          for (c = 0; c < 3; c++)
            p[c] = v8[c] / 255.0f;
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
  g_free (job.lap);
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
    "name",            "forensics:echo",
    "title",           _("Echo Edge Filter"),
    "categories",      "forensics:analysis",
    "description",     _("Shows the Laplacian of each channel, strongly "
                         "amplified (after Sherloq): detail and noise light "
                         "up, a region blurred or out of focus while its "
                         "surroundings are sharp stays dark. An indicator, "
                         "not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Echo Edge Filter..."),
    NULL);
}

#endif
