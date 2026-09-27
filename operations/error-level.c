/*
 * Error Level Analysis (ELA), a GEGL operation
 *
 * error-level.c
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Error level analysis (Krawetz, "A Picture's Worth...", Black Hat 2007):
 * the image is saved again as a JPEG at a known quality and the
 * difference between the image and that copy is shown, amplified. Parts
 * of a JPEG image that were saved as often (and on the same block grid)
 * as the rest change about as much; a part pasted in from elsewhere, or
 * painted over, often changes more or less. The operation does the JPEG
 * round trip itself, in memory, with libjpeg (forensics-jpeg.h), on the
 * image's 8 bit encoded values: that is what a JPEG file of the image
 * holds. Written for this operation; GIMP-ELA (Alfredo Torre, MIT) and
 * elsamuko's ELA script (GPL-3+) were read for what users expect: a
 * Difference of the image and a JPEG copy, stretched with Levels.
 *
 * Output: |image - JPEG copy| x scale, per channel (color), of the JPEG
 * luma Y (luminance) or the largest channel (maximum), in the image's
 * encoded values (0 to 1 is 0 to 255 levels of the 8 bit image), or with
 * auto levels scaled so that the 99.4th percentile of the error is 1
 * (as GIMP's Levels "Auto Input Levels", which the old scripts used,
 * with one scale for all channels to keep their colors comparable).
 * Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_ela_chroma)
  enum_value (FORENSICS_ELA_CHROMA_420, "4:2:0", N_("4:2:0 (most cameras)"))
  enum_value (FORENSICS_ELA_CHROMA_422, "4:2:2", N_("4:2:2"))
  enum_value (FORENSICS_ELA_CHROMA_444, "4:4:4", N_("4:4:4 (none)"))
enum_end (ForensicsElaChroma)

enum_start (forensics_ela_output)
  enum_value (FORENSICS_ELA_OUTPUT_COLOR,     "color",     N_("Color (per channel)"))
  enum_value (FORENSICS_ELA_OUTPUT_LUMINANCE, "luminance", N_("Luminance"))
  enum_value (FORENSICS_ELA_OUTPUT_MAXIMUM,   "maximum",   N_("Largest channel"))
enum_end (ForensicsElaOutput)

property_int (quality, _("JPEG quality"), 90)
  description (_("The quality at which the image is saved again as a JPEG "
                 "(0 to 100, as in GIMP's JPEG export)"))
  value_range (0, 100)

property_double (scale, _("Error scale"), 20.0)
  description (_("How much the difference between the image and its JPEG "
                 "copy is amplified"))
  value_range (0.0, 100.0)
  ui_range (1.0, 50.0)
  ui_digits (1)

property_boolean (auto_levels, _("Auto levels"), FALSE)
  description (_("Scale the error so that the brightest 0.6 % of it is "
                 "white, instead of by the error scale (the analysis "
                 "then needs the whole image)"))

property_enum (output, _("Show"), ForensicsElaOutput, forensics_ela_output,
               FORENSICS_ELA_OUTPUT_COLOR)
  description (_("The error of each channel (color), of the brightness as "
                 "JPEG stores it (luminance), or of the channel that "
                 "changed most"))

property_enum (chroma, _("Chroma subsampling"), ForensicsElaChroma,
               forensics_ela_chroma, FORENSICS_ELA_CHROMA_420)
  description (_("The chroma subsampling of the JPEG copy. 4:2:0 is what "
                 "most cameras and programs use."))

property_int (grid_x, _("Grid offset x"), 0)
  description (_("Where the 8 x 8 blocks of the original JPEG file start, "
                 "in pixels from the left edge. 0 unless the image was "
                 "cropped: after cropping c pixels from the left, "
                 "(16 - c mod 16) mod 16. 8 to 15 matter for the 16 x 16 "
                 "chroma blocks of 4:2:0 and 4:2:2 only."))
  value_range (0, 15)

property_int (grid_y, _("Grid offset y"), 0)
  description (_("Where the 8 x 8 blocks of the original JPEG file start, "
                 "in pixels from the top edge (see grid offset x)"))
  value_range (0, 15)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     error_level
#define GEGL_OP_C_SOURCE error-level.c

#include "gegl-op.h"
#include "forensics-jpeg.h"

/* GIMP's Levels "Auto Input Levels" clips 0.6 % at the top */
#define AUTO_LEVELS_TOP 0.006
/* histogram bins per 8 bit level, and levels, for auto levels */
#define BINS_PER_LEVEL 4
#define N_BINS         (256 * BINS_PER_LEVEL + 1)

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

static void
canvas_for (GeglOperation *operation,
            FxCanvas      *c)
{
  GeglProperties *o    = GEGL_PROPERTIES (operation);
  GeglRectangle   bbox = get_bounding_box (operation);

  fx_canvas_init (c, &bbox, o->grid_x, o->grid_y, o->chroma);
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  FxCanvas        c;
  GeglRectangle   need, piece;

  (void) input_pad;
  canvas_for (operation, &c);
  if (o->auto_levels)
    return c.bbox;
  if (! gegl_rectangle_intersect (&need, roi, &c.bbox))
    return need;
  piece = fx_canvas_piece (&c, &need);
  return fx_canvas_source (&c, &piece);
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  FxCanvas        c;
  GeglRectangle   r, piece;

  (void) input_pad;
  canvas_for (operation, &c);
  if (o->auto_levels)
    return c.bbox;
  /* a pixel changes every block of every piece that reads it: its MCU
   * and, with subsampled chroma, the neighbouring ones */
  if (! gegl_rectangle_intersect (&r, roi, &c.bbox))
    return r;
  piece = fx_canvas_piece (&c, &r);
  piece.x      -= c.margin_x;
  piece.y      -= c.margin_y;
  piece.width  += 2 * c.margin_x;
  piece.height += 2 * c.margin_y;
  gegl_rectangle_intersect (&r, &piece, &c.bbox);
  return r;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);

  if (o->auto_levels)
    return get_bounding_box (operation);
  return *roi;
}

typedef struct
{
  GeglOperation       *operation;
  GeglBuffer          *input;
  GeglBuffer          *output;
  const Babl          *format;
  FxCanvas             canvas;
  GeglRectangle        roi;
  gint                *starts;
  gint                 quality;
  gint                 chroma;
  gint                 mode;
  gfloat               gain;    /* output = error in levels x gain */
  gboolean             histogram_only;
  guint64            (*hist)[N_BINS];  /* one per piece */
  volatile gint        failed;
} Job;

static inline gfloat
luma (const guint8 *p)
{
  /* JFIF's Y, which JPEG compresses at full resolution */
  return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
}

static void
do_piece (Job  *job,
          gint  i)
{
  GeglRectangle need, piece;
  guint8       *orig, *copy;
  gfloat       *out = NULL;
  gint          x, y;

  need.x      = job->roi.x;
  need.width  = job->roi.width;
  need.y      = job->starts[i];
  need.height = job->starts[i + 1] - job->starts[i];
  piece = fx_canvas_piece (&job->canvas, &need);

  orig = g_new (guint8, (gsize) piece.width * piece.height * 3);
  copy = g_new (guint8, (gsize) piece.width * piece.height * 3);
  fx_read_piece (job->input, job->format, &job->canvas, &piece, orig);
  if (! fx_jpeg_round_trip (orig, piece.width, piece.height, job->quality,
                            job->chroma, copy))
    {
      g_atomic_int_set (&job->failed, 1);
      memcpy (copy, orig, (gsize) piece.width * piece.height * 3);
    }

  if (! job->histogram_only)
    {
      out = g_new (gfloat, (gsize) need.width * need.height * 4);
      /* alpha as it is */
      gegl_buffer_get (job->input, &need, 1.0, job->format, out,
                       GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    }

  for (y = 0; y < need.height; y++)
    {
      gsize         off = ((gsize) (need.y + y - piece.y) * piece.width +
                           (need.x - piece.x)) * 3;
      const guint8 *a   = orig + off;
      const guint8 *b   = copy + off;
      gfloat       *o   = out ? out + (gsize) y * need.width * 4 : NULL;

      for (x = 0; x < need.width; x++, a += 3, b += 3)
        {
          gfloat e[3];

          switch (job->mode)
            {
            case FORENSICS_ELA_OUTPUT_COLOR:
              e[0] = abs (a[0] - b[0]);
              e[1] = abs (a[1] - b[1]);
              e[2] = abs (a[2] - b[2]);
              break;
            case FORENSICS_ELA_OUTPUT_LUMINANCE:
              e[0] = e[1] = e[2] = fabsf (luma (a) - luma (b));
              break;
            default:
              e[0] = e[1] = e[2] = MAX (MAX (abs (a[0] - b[0]),
                                             abs (a[1] - b[1])),
                                        abs (a[2] - b[2]));
              break;
            }

          if (job->histogram_only)
            {
              gint c, n = job->mode == FORENSICS_ELA_OUTPUT_COLOR ? 3 : 1;

              for (c = 0; c < n; c++)
                job->hist[i][CLAMP ((gint) (e[c] * BINS_PER_LEVEL + 0.5f),
                                    0, N_BINS - 1)]++;
            }
          else
            {
              o[4 * x + 0] = MIN (e[0] * job->gain, 1e6f);
              o[4 * x + 1] = MIN (e[1] * job->gain, 1e6f);
              o[4 * x + 2] = MIN (e[2] * job->gain, 1e6f);
              /* alpha: NaN becomes 0, as in the colors */
              if (! (o[4 * x + 3] == o[4 * x + 3]))
                o[4 * x + 3] = 0.0f;
            }
        }
    }

  if (out)
    {
      gegl_buffer_set (job->output, &need, 0, job->format, out,
                       GEGL_AUTO_ROWSTRIDE);
      g_free (out);
    }
  g_free (orig);
  g_free (copy);
}

static void
do_pieces (gsize  first,
           gsize  n,
           gpointer data)
{
  Job  *job = data;
  gsize i;

  for (i = first; i < first + n; i++)
    do_piece (job, (gint) i);
}

static void
run (Job *job,
     gint n_pieces)
{
  /* a thread costs about as much as a few thousand pixels; a piece is
   * at least 64 rows */
  gdouble piece = (gdouble) job->roi.width * MIN (job->roi.height, 64);

  gegl_parallel_distribute_range (n_pieces, 4096.0 / MAX (piece, 1.0),
                                  do_pieces, job);
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
  gint            n_pieces, n_threads;

  (void) level;
  memset (&job, 0, sizeof job);
  job.operation = operation;
  job.input     = input;
  job.output    = output;
  job.format    = work_format (operation);
  job.quality   = o->quality;
  job.chroma    = o->chroma;
  job.mode      = o->output;
  canvas_for (operation, &job.canvas);
  if (! gegl_rectangle_intersect (&job.roi, result, &job.canvas.bbox))
    return TRUE;

  g_object_get (gegl_config (), "threads", &n_threads, NULL);
  n_pieces = fx_split_rows (&job.canvas, &job.roi, 2 * n_threads, &job.starts);

  job.gain = (gfloat) (o->scale / 255.0);
  if (o->auto_levels)
    {
      GeglRectangle roi = job.roi;
      guint64       total[N_BINS];
      guint64       count = 0, seen = 0;
      gint          i, b, top = N_BINS - 1;

      /* the whole image first, for the histogram of the error */
      g_free (job.starts);
      job.roi  = job.canvas.bbox;
      n_pieces = fx_split_rows (&job.canvas, &job.roi, 2 * n_threads,
                                &job.starts);
      job.hist = g_malloc0 (sizeof (*job.hist) * n_pieces);
      job.histogram_only = TRUE;
      run (&job, n_pieces);
      memset (total, 0, sizeof total);
      for (i = 0; i < n_pieces; i++)
        for (b = 0; b < N_BINS; b++)
          total[b] += job.hist[i][b];
      g_free (job.hist);
      job.hist = NULL;
      for (b = 0; b < N_BINS; b++)
        count += total[b];
      /* the smallest level with at most 0.6 % above it */
      for (b = 0; b < N_BINS; b++)
        {
          seen += total[b];
          if ((gdouble) (count - seen) <= AUTO_LEVELS_TOP * count)
            {
              top = b;
              break;
            }
        }
      /* at least one level: an image without error stays black */
      job.gain = 1.0f / ((gfloat) MAX (top, BINS_PER_LEVEL) / BINS_PER_LEVEL);

      g_free (job.starts);
      job.roi  = roi;
      n_pieces = fx_split_rows (&job.canvas, &job.roi, 2 * n_threads,
                                &job.starts);
      job.histogram_only = FALSE;
    }
  run (&job, n_pieces);
  g_free (job.starts);

  if (job.failed)
    g_warning ("forensics:error-level: libjpeg failed; that part shows no error");
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
  /* the pieces are cut on JPEG block lines here, not by GEGL */
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:error-level",
    "title",           _("Error Level Analysis"),
    "categories",      "forensics:analysis",
    "description",     _("Saves the image again as a JPEG at a known "
                         "quality and shows how much each pixel changed. "
                         "Parts with another compression history (pasted "
                         "or painted over) often stand out. An indicator, "
                         "not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Error Level Analysis..."),
    NULL);
}

#endif
