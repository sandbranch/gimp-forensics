/*
 * Noise analysis, a GEGL operation
 *
 * noise.c
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
 * The noise residual: the image minus a denoised copy of it, amplified,
 * as in the Noise Analysis of Forensically (Jonas Wagner), which takes a
 * median filter "to isolate the noise". Parts of a picture taken with
 * one camera at one ISO share one noise level; a region pasted from
 * another picture, painted over, blurred or airbrushed usually has more
 * or less noise. The denoised copy is a median of a (2r + 1)^2 square
 * (r = 1 to 3), or the smoothing of the first level of the "a trous"
 * wavelet transform with the B3 spline (1 4 6 4 1) / 16, whose residual
 * is the finest wavelet detail. Written for this operation.
 *
 * Shown: |residual| per channel, of the luma Y' (Rec. 709 weights on the
 * image's encoded values) or the signed residual of Y' around gray, times
 * the amplitude; optionally averaged over a window (a map of the local
 * noise level) or scaled so that the 99.4th percentile is white (auto
 * levels, which needs the whole image). Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_noise_method)
  enum_value (FORENSICS_NOISE_MEDIAN,  "median",  N_("Median"))
  enum_value (FORENSICS_NOISE_WAVELET, "wavelet", N_("Wavelet (finest detail)"))
enum_end (ForensicsNoiseMethod)

enum_start (forensics_noise_mode)
  enum_value (FORENSICS_NOISE_COLOR,     "color",     N_("Color (per channel)"))
  enum_value (FORENSICS_NOISE_LUMINANCE, "luminance", N_("Luminance"))
  enum_value (FORENSICS_NOISE_SIGNED,    "signed",    N_("Luminance, signed"))
enum_end (ForensicsNoiseMode)

property_enum (method, _("Denoise with"), ForensicsNoiseMethod,
               forensics_noise_method, FORENSICS_NOISE_MEDIAN)
  description (_("How the noise is taken out of the image: a median, or "
                 "the finest level of a wavelet transform"))

property_int (radius, _("Median radius"), 1)
  description (_("The median of a square of 2 x radius + 1 pixels"))
  value_range (1, 3)

property_enum (mode, _("Show"), ForensicsNoiseMode, forensics_noise_mode,
               FORENSICS_NOISE_COLOR)
  description (_("The size of the noise of each channel, of the "
                 "brightness, or the brightness noise itself around gray"))

property_double (amplitude, _("Noise amplitude"), 10.0)
  description (_("How much the noise is amplified"))
  value_range (0.0, 1000.0)
  ui_range (1.0, 100.0)
  ui_gamma (2.0)

property_int (average, _("Average over"), 0)
  description (_("Average the size of the noise over a square of this many "
                 "pixels, for a map of the local noise level (0: no "
                 "averaging; not for the signed noise)"))
  value_range (0, 64)

property_boolean (auto_levels, _("Auto levels"), FALSE)
  description (_("Scale the noise so that the brightest 0.6 % of it is "
                 "white, instead of by the amplitude (the analysis then "
                 "needs the whole image)"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     noise
#define GEGL_OP_C_SOURCE noise.c

#include "gegl-op.h"
#include <math.h>
#include <string.h>

#define AUTO_LEVELS_TOP 0.006
#define N_BINS          4096   /* of 0 to 1, for auto levels */

static const Babl *
work_format (GeglOperation *operation)
{
  return babl_format_with_space ("R'G'B'A float",
                                 gegl_operation_get_source_space (operation,
                                                                  "input"));
}

static void
prepare (GeglOperation *operation)
{
  const Babl *format = work_format (operation);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation,
                                                                    "input");
  GeglRectangle        r  = { 0, 0, 0, 0 };

  if (in)
    r = *in;
  return r;
}

/* how far the denoising reaches */
static gint
denoise_reach (GeglProperties *o)
{
  return o->method == FORENSICS_NOISE_MEDIAN ? o->radius : 2;
}

static gboolean
averaged (GeglProperties *o)
{
  return o->average > 1 && o->mode != FORENSICS_NOISE_SIGNED;
}

static void
average_extent (GeglProperties *o, gint *before, gint *after)
{
  *before = averaged (o) ? (o->average - 1) / 2 : 0;
  *after  = averaged (o) ? o->average - 1 - *before : 0;
}

static GeglRectangle
grow (const GeglRectangle *r, gint before, gint after)
{
  GeglRectangle g = *r;

  g.x -= before;
  g.y -= before;
  g.width  += before + after;
  g.height += before + after;
  return g;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglProperties *o    = GEGL_PROPERTIES (operation);
  GeglRectangle   bbox = get_bounding_box (operation);
  GeglRectangle   r;
  gint            before, after, reach = denoise_reach (o);

  (void) input_pad;
  average_extent (o, &before, &after);
  r = o->auto_levels ? bbox : *roi;
  r = grow (&r, before + reach, after + reach);
  gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  GeglProperties *o    = GEGL_PROPERTIES (operation);
  GeglRectangle   bbox = get_bounding_box (operation);
  GeglRectangle   r;
  gint            before, after, reach = denoise_reach (o);

  (void) input_pad;
  if (o->auto_levels)
    return bbox;
  average_extent (o, &before, &after);
  /* (the window of a pixel reaches before to the left and after to the
   * right: a change reaches after to the left and before to the right) */
  r = grow (roi, after + reach, before + reach);
  gegl_rectangle_intersect (&r, &r, &bbox);
  return r;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);

  return o->auto_levels ? get_bounding_box (operation) : *roi;
}

static inline gfloat
sane (gfloat v)
{
  if (v != v)
    return 0.0f;
  if (v > 1e6f)
    return 1.0f;
  if (v < -1e6f)
    return 0.0f;
  return v;
}

#define SORT2(a, b) { if (v[a] > v[b]) { gfloat t_ = v[a]; v[a] = v[b]; v[b] = t_; } }

static inline gfloat
median9 (gfloat *v)
{
  /* the median of 9 with 19 compare-exchanges (Paeth) */
  SORT2 (1, 2); SORT2 (4, 5); SORT2 (7, 8); SORT2 (0, 1); SORT2 (3, 4);
  SORT2 (6, 7); SORT2 (1, 2); SORT2 (4, 5); SORT2 (7, 8); SORT2 (0, 3);
  SORT2 (5, 8); SORT2 (4, 7); SORT2 (3, 6); SORT2 (1, 4); SORT2 (2, 5);
  SORT2 (4, 7); SORT2 (4, 2); SORT2 (6, 4); SORT2 (4, 2);
  return v[4];
}

static gfloat
select_median (gfloat *v, gint n)
{
  /* quickselect of the middle one (n odd) */
  gint lo = 0, hi = n - 1, k = n / 2;

  while (lo < hi)
    {
      gfloat pivot = v[(lo + hi) / 2];
      gint   i = lo, j = hi;

      while (i <= j)
        {
          while (v[i] < pivot)
            i++;
          while (v[j] > pivot)
            j--;
          if (i <= j)
            {
              gfloat t = v[i];

              v[i] = v[j];
              v[j] = t;
              i++;
              j--;
            }
        }
      if (k <= j)
        hi = j;
      else if (k >= i)
        lo = i;
      else
        break;
    }
  return v[k];
}

typedef struct
{
  GeglBuffer   *input;
  GeglBuffer   *output;
  const Babl   *format;
  GeglRectangle bbox;
  GeglRectangle roi;
  gint          method, radius, mode;
  gint          before, after;   /* the averaging window */
  gfloat        gain;
  gint          n_bands, band_rows;
  gboolean      histogram_only;
  guint32     (*hist)[N_BINS];
} Job;

/* the size of the noise (or the signed noise) of the band's rows of
 * roi: out has need->width x need->height x 4 floats (alpha from the
 * image) */
static void
noise_of (Job                 *job,
          const GeglRectangle *need,
          gfloat              *out)
{
  gint          reach = job->method == FORENSICS_NOISE_MEDIAN ? job->radius : 2;
  gint          nc    = job->mode == FORENSICS_NOISE_COLOR ? 3 : 1;
  GeglRectangle res   = grow (need, job->before, job->after);  /* residuals */
  GeglRectangle src   = grow (&res, reach, reach);             /* pixels */
  GeglRectangle read;
  gfloat       *pix, *chan, *r;
  gint          x, y, c;

  gegl_rectangle_intersect (&read, &src, &job->bbox);
  pix  = g_new (gfloat, (gsize) read.width * read.height * 4);
  gegl_buffer_get (job->input, &read, 1.0, job->format, pix,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  /* the channels (or Y') of src, with the edge pixels repeated beyond
   * the image */
  chan = g_new (gfloat, (gsize) src.width * src.height * nc);
  for (y = 0; y < src.height; y++)
    for (x = 0; x < src.width; x++)
      {
        gint          sx = CLAMP (src.x + x, read.x, read.x + read.width - 1) - read.x;
        gint          sy = CLAMP (src.y + y, read.y, read.y + read.height - 1) - read.y;
        const gfloat *p  = pix + ((gsize) sy * read.width + sx) * 4;
        gfloat       *q  = chan + ((gsize) y * src.width + x) * nc;

        if (nc == 3)
          {
            q[0] = sane (p[0]);
            q[1] = sane (p[1]);
            q[2] = sane (p[2]);
          }
        else
          q[0] = 0.2126f * sane (p[0]) + 0.7152f * sane (p[1]) + 0.0722f * sane (p[2]);
      }

  /* the residual on res */
  r = g_new (gfloat, (gsize) res.width * res.height * nc);
  if (job->method == FORENSICS_NOISE_WAVELET)
    {
      /* B3 spline, separable: rows, then columns */
      static const gfloat k[5] = { 1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f };
      gfloat *t = g_new (gfloat, (gsize) res.width * src.height * nc);

      for (y = 0; y < src.height; y++)
        for (x = 0; x < res.width; x++)
          for (c = 0; c < nc; c++)
            {
              const gfloat *s = chan + ((gsize) y * src.width + x) * nc + c;
              gfloat        v = 0;
              gint          i;

              for (i = 0; i < 5; i++)
                v += k[i] * s[i * nc];
              t[((gsize) y * res.width + x) * nc + c] = v;
            }
      for (y = 0; y < res.height; y++)
        for (x = 0; x < res.width; x++)
          for (c = 0; c < nc; c++)
            {
              gfloat v = 0;
              gint   i;

              for (i = 0; i < 5; i++)
                v += k[i] * t[((gsize) (y + i) * res.width + x) * nc + c];
              r[((gsize) y * res.width + x) * nc + c] =
                chan[((gsize) (y + 2) * src.width + x + 2) * nc + c] - v;
            }
      g_free (t);
    }
  else
    {
      gint   side = 2 * job->radius + 1, n = side * side;
      gfloat v[49];

      for (y = 0; y < res.height; y++)
        for (x = 0; x < res.width; x++)
          for (c = 0; c < nc; c++)
            {
              gint   i, j, m = 0;
              gfloat med;

              for (j = 0; j < side; j++)
                {
                  const gfloat *s = chan + ((gsize) (y + j) * src.width + x) * nc + c;

                  for (i = 0; i < side; i++)
                    v[m++] = s[i * nc];
                }
              med = n == 9 ? median9 (v) : select_median (v, n);
              r[((gsize) y * res.width + x) * nc + c] =
                chan[((gsize) (y + reach) * src.width + x + reach) * nc + c] - med;
            }
    }

  /* the size of the noise averaged over the window, clipped to the
   * image: an integral image of |r| over the part of res in the image */
  if (job->mode != FORENSICS_NOISE_SIGNED && job->before + job->after > 0)
    {
      GeglRectangle in;
      gdouble      *sum;
      gint          iw;

      gegl_rectangle_intersect (&in, &res, &job->bbox);
      iw  = in.width + 1;
      sum = g_new0 (gdouble, (gsize) iw * (in.height + 1) * nc);
      for (y = 0; y < in.height; y++)
        for (c = 0; c < nc; c++)
          {
            gdouble acc = 0;

            for (x = 0; x < in.width; x++)
              {
                acc += fabsf (r[((gsize) (in.y + y - res.y) * res.width +
                                 (in.x + x - res.x)) * nc + c]);
                sum[((gsize) (y + 1) * iw + x + 1) * nc + c] =
                  sum[((gsize) y * iw + x + 1) * nc + c] + acc;
              }
          }
      for (y = 0; y < need->height; y++)
        for (x = 0; x < need->width; x++)
          for (c = 0; c < nc; c++)
            {
              gint gx = need->x + x - in.x, gy = need->y + y - in.y;
              gint x1 = MAX (gx - job->before, 0), y1 = MAX (gy - job->before, 0);
              gint x2 = MIN (gx + job->after + 1, in.width);
              gint y2 = MIN (gy + job->after + 1, in.height);
              gdouble t = sum[((gsize) y2 * iw + x2) * nc + c] -
                          sum[((gsize) y1 * iw + x2) * nc + c] -
                          sum[((gsize) y2 * iw + x1) * nc + c] +
                          sum[((gsize) y1 * iw + x1) * nc + c];

              out[((gsize) y * need->width + x) * 4 + c] =
                (gfloat) (t / ((x2 - x1) * (y2 - y1)));
            }
      g_free (sum);
    }

  /* the colors of need: |r| (or its average, in out already) or the
   * signed r, times the gain */
  for (y = 0; y < need->height; y++)
    for (x = 0; x < need->width; x++)
      {
        gfloat *o = out + ((gsize) y * need->width + x) * 4;
        gfloat  e[3];

        for (c = 0; c < nc; c++)
          {
            gfloat v = r[((gsize) (y + job->before) * res.width + x + job->before) * nc + c];

            if (job->mode == FORENSICS_NOISE_SIGNED)
              e[c] = v;
            else if (job->before + job->after == 0)
              e[c] = fabsf (v);
            else
              e[c] = o[c];
          }
        if (nc == 1)
          e[1] = e[2] = e[0];

        if (job->histogram_only)
          {
            for (c = 0; c < nc; c++)
              job->hist[0][CLAMP ((gint) (fabsf (e[c]) * (N_BINS - 1) + 0.5f), 0, N_BINS - 1)]++;
          }
        else if (job->mode == FORENSICS_NOISE_SIGNED)
          o[0] = o[1] = o[2] = 0.5f + e[0] * job->gain;
        else
          {
            o[0] = e[0] * job->gain;
            o[1] = e[1] * job->gain;
            o[2] = e[2] * job->gain;
          }
      }
  /* alpha */
  if (! job->histogram_only)
    for (y = 0; y < need->height; y++)
      for (x = 0; x < need->width; x++)
        {
          const gfloat *p = pix + ((gsize) (need->y + y - read.y) * read.width +
                                   need->x + x - read.x) * 4;
          gfloat        a = p[3];

          out[((gsize) y * need->width + x) * 4 + 3] = a == a ? a : 0.0f;
        }

  g_free (r);
  g_free (chan);
  g_free (pix);
}

static void
do_bands (gsize    first,
          gsize    n,
          gpointer data)
{
  Job  *job = data;
  gsize i;

  for (i = first; i < first + n; i++)
    {
      GeglRectangle need = job->roi;
      gfloat       *out;
      Job           mine = *job;

      need.y      = job->roi.y + (gint) i * job->band_rows;
      need.height = MIN (job->band_rows, job->roi.y + job->roi.height - need.y);
      if (need.height <= 0)
        continue;
      out = g_new (gfloat, (gsize) need.width * need.height * 4);
      if (job->histogram_only)
        mine.hist = job->hist + i;
      noise_of (&mine, &need, out);
      if (! job->histogram_only)
        gegl_buffer_set (job->output, &need, 0, job->format, out,
                         GEGL_AUTO_ROWSTRIDE);
      g_free (out);
    }
}

static void
run (Job *job)
{
  gint n_threads;

  g_object_get (gegl_config (), "threads", &n_threads, NULL);
  job->band_rows = MAX ((job->roi.height + 2 * n_threads - 1) / (2 * n_threads), 16);
  job->n_bands   = (job->roi.height + job->band_rows - 1) / job->band_rows;
  gegl_parallel_distribute_range (job->n_bands,
                                  4096.0 / MAX ((gdouble) job->roi.width * job->band_rows, 1.0),
                                  do_bands, job);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  Job             job;

  (void) level;
  memset (&job, 0, sizeof job);
  job.input  = input;
  job.output = output;
  job.format = work_format (operation);
  job.bbox   = get_bounding_box (operation);
  job.method = o->method;
  job.radius = CLAMP (o->radius, 1, 3);
  job.mode   = o->mode;
  job.gain   = (gfloat) o->amplitude;
  average_extent (o, &job.before, &job.after);
  if (! gegl_rectangle_intersect (&job.roi, result, &job.bbox))
    return TRUE;

  if (o->auto_levels)
    {
      GeglRectangle roi = job.roi;
      guint64       total[N_BINS], count = 0, seen = 0;
      gint          i, b, top = N_BINS - 1;

      job.roi = job.bbox;
      job.histogram_only = TRUE;
      {
        gint n_threads;

        g_object_get (gegl_config (), "threads", &n_threads, NULL);
        job.band_rows = MAX ((job.roi.height + 2 * n_threads - 1) / (2 * n_threads), 16);
        job.n_bands   = (job.roi.height + job.band_rows - 1) / job.band_rows;
      }
      job.hist = g_malloc0 (sizeof (*job.hist) * job.n_bands);
      gegl_parallel_distribute_range (job.n_bands,
                                      4096.0 / MAX ((gdouble) job.roi.width * job.band_rows, 1.0),
                                      do_bands, &job);
      memset (total, 0, sizeof total);
      for (i = 0; i < job.n_bands; i++)
        for (b = 0; b < N_BINS; b++)
          total[b] += job.hist[i][b];
      g_free (job.hist);
      job.hist = NULL;
      for (b = 0; b < N_BINS; b++)
        count += total[b];
      for (b = 0; b < N_BINS; b++)
        {
          seen += total[b];
          if ((gdouble) (count - seen) <= AUTO_LEVELS_TOP * count)
            {
              top = b;
              break;
            }
        }
      /* the signed noise: the 99.4th percentile of its size at half
       * gray from the middle */
      job.gain = 1.0f / ((gfloat) MAX (top, 1) / (N_BINS - 1));
      if (o->mode == FORENSICS_NOISE_SIGNED)
        job.gain *= 0.5f;
      job.roi = roi;
      job.histogram_only = FALSE;
    }
  run (&job);
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
  /* (the auto levels need one scale for the whole image) */
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:noise",
    "title",           _("Noise Analysis"),
    "categories",      "forensics:analysis",
    "description",     _("Shows the noise of the image: the image minus a "
                         "denoised copy, amplified. A region pasted from "
                         "another picture, painted over or blurred often "
                         "has more or less noise than the rest. An "
                         "indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Noise Analysis..."),
    NULL);
}

#endif
