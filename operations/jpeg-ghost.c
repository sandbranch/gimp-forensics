/*
 * JPEG Ghost, a GEGL operation
 *
 * jpeg-ghost.c
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
 * JPEG ghosts (H. Farid, "Exposing Digital Forgeries from JPEG Ghosts",
 * IEEE Transactions on Information Forensics and Security 4 (1), 2009):
 * a part of an image that was saved as a JPEG at a lower quality q0
 * before it was pasted in, and the whole then saved at q1, changes least
 * when the image is saved again at q0. The image is saved again at a
 * sweep of qualities q (forensics-jpeg.h, in memory); for each q the
 * squared difference, averaged over the three channels and a b x b
 * window (Farid's equation 3, in 8 bit levels), is
 *
 *   delta (x, y, q) = 1 / (3 b^2) sum_window sum_i (f_i - f_q,i)^2
 *
 * and normalised per pixel over the sweep (equation 4):
 *
 *   d (x, y, q) = (delta (q) - min_q delta) / (max_q delta - min_q delta)
 *
 * Shown: d at the chosen quality (dark where that quality fits best), the
 * root of delta at it (the averaged error, in levels, times a scale), or
 * the quality of the sweep at which delta is smallest. The window is
 * centred on the pixel (Farid's sums start at it) and clipped to the
 * image. Gray, or a blue-green-red ramp as in Bernardo Bulgarelli
 * Labronici's GIMP 2 JPEG Ghost plug-in (GPL-3+), which was read for what
 * users expect; the code is written for this operation. Alpha is passed
 * through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_ghost_chroma)
  enum_value (FORENSICS_GHOST_CHROMA_420, "4:2:0", N_("4:2:0 (most cameras)"))
  enum_value (FORENSICS_GHOST_CHROMA_422, "4:2:2", N_("4:2:2"))
  enum_value (FORENSICS_GHOST_CHROMA_444, "4:4:4", N_("4:4:4 (none)"))
enum_end (ForensicsGhostChroma)

enum_start (forensics_ghost_mode)
  enum_value (FORENSICS_GHOST_NORMALIZED, "normalized",
              N_("Difference at the quality, normalized over the sweep"))
  enum_value (FORENSICS_GHOST_DIFFERENCE, "difference",
              N_("Difference at the quality"))
  enum_value (FORENSICS_GHOST_MINIMUM,    "minimum",
              N_("Quality of the smallest difference"))
enum_end (ForensicsGhostMode)

property_int (quality, _("JPEG quality"), 70)
  description (_("The quality at which the difference is shown. A region "
                 "that was saved at this quality before shows up dark."))
  value_range (0, 100)

property_int (sweep_min, _("Sweep from quality"), 50)
  description (_("The lowest quality of the sweep over which the "
                 "difference is normalized (and the minimum searched)"))
  value_range (0, 100)

property_int (sweep_max, _("Sweep to quality"), 95)
  description (_("The highest quality of the sweep"))
  value_range (0, 100)

property_int (sweep_step, _("Sweep step"), 5)
  description (_("The step between the qualities of the sweep; each "
                 "quality is one more JPEG round trip"))
  value_range (1, 50)

property_int (block_size, _("Averaging window"), 16)
  description (_("The width of the square window over which the squared "
                 "difference is averaged (Farid uses 16)"))
  value_range (1, 64)

property_enum (mode, _("Show"), ForensicsGhostMode, forensics_ghost_mode,
               FORENSICS_GHOST_NORMALIZED)
  description (_("The normalized difference at the quality (Farid's "
                 "figures), the averaged difference itself, or the quality "
                 "of the sweep at which the difference is smallest (black "
                 "the lowest, white the highest)"))

property_double (scale, _("Difference scale"), 10.0)
  description (_("How much the averaged difference is amplified when it "
                 "is shown itself"))
  value_range (0.0, 100.0)
  ui_range (1.0, 50.0)

property_boolean (colormap, _("Color ramp"), FALSE)
  description (_("Show the values from blue (0) over green to red (1) "
                 "instead of gray"))

property_enum (chroma, _("Chroma subsampling"), ForensicsGhostChroma,
               forensics_ghost_chroma, FORENSICS_GHOST_CHROMA_420)
  description (_("The chroma subsampling of the JPEG copies"))

property_int (grid_x, _("Grid offset x"), 0)
  description (_("Where the 8 x 8 blocks of the original JPEG file start, "
                 "in pixels from the left edge (see Error Level Analysis)"))
  value_range (0, 15)

property_int (grid_y, _("Grid offset y"), 0)
  description (_("Where the 8 x 8 blocks of the original JPEG file start, "
                 "in pixels from the top edge"))
  value_range (0, 15)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     jpeg_ghost
#define GEGL_OP_C_SOURCE jpeg-ghost.c

#include "gegl-op.h"
#include "forensics-jpeg.h"

#define MAX_QUALITIES 102

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

/* the window around a pixel: from x - before to x + after */
static void
window_extent (GeglProperties *o, gint *before, gint *after)
{
  *before = (o->block_size - 1) / 2;
  *after  = o->block_size - 1 - *before;
}

/* the pixels the averaged difference of need reads, within the image */
static GeglRectangle
window_area (gint                 before,
             gint                 after,
             const FxCanvas      *c,
             const GeglRectangle *need)
{
  GeglRectangle r = *need;

  r.x      -= before;
  r.y      -= before;
  r.width  += before + after;
  r.height += before + after;
  gegl_rectangle_intersect (&r, &r, &c->bbox);
  return r;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  FxCanvas        c;
  GeglRectangle   need, area, piece;
  gint            before, after;

  (void) input_pad;
  canvas_for (operation, &c);
  if (! gegl_rectangle_intersect (&need, roi, &c.bbox))
    return need;
  window_extent (o, &before, &after);
  area  = window_area (before, after, &c, &need);
  piece = fx_canvas_piece (&c, &area);
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
  gint            before, after;

  (void) input_pad;
  canvas_for (operation, &c);
  if (! gegl_rectangle_intersect (&r, roi, &c.bbox))
    return r;
  /* the blocks the change reaches (with the chroma margin), then every
   * window that overlaps them */
  piece = fx_canvas_piece (&c, &r);
  window_extent (o, &before, &after);
  piece.x      -= c.margin_x + after;
  piece.y      -= c.margin_y + after;
  piece.width  += 2 * c.margin_x + before + after;
  piece.height += 2 * c.margin_y + before + after;
  gegl_rectangle_intersect (&r, &piece, &c.bbox);
  return r;
}

/* the qualities of the sweep, with the shown one; returns how many, and
 * which of them is the shown one */
static gint
sweep_qualities (GeglProperties *o, gint *q, gint *shown)
{
  gint lo = MIN (o->sweep_min, o->sweep_max);
  gint hi = MAX (o->sweep_min, o->sweep_max);
  gint n  = 0, v, i;

  for (v = lo; v <= hi && n < MAX_QUALITIES - 1; v += MAX (o->sweep_step, 1))
    q[n++] = v;
  *shown = -1;
  for (i = 0; i < n; i++)
    if (q[i] == o->quality)
      *shown = i;
  if (*shown < 0)
    {
      /* in order, so that the minimum map stays ordered */
      for (i = n; i > 0 && q[i - 1] > o->quality; i--)
        q[i] = q[i - 1];
      q[i] = o->quality;
      *shown = i;
      n++;
    }
  return n;
}

typedef struct
{
  GeglBuffer   *input;
  GeglBuffer   *output;
  const Babl   *format;
  FxCanvas      canvas;
  GeglRectangle roi;
  gint         *starts;
  gint          chroma;
  gint          before, after;
  gint          q[MAX_QUALITIES];
  gint          n_q, shown;
  gint          mode;
  gboolean      colormap;
  gfloat        scale;
  gint          q_lo, q_hi;   /* the sweep, for the minimum map */
  volatile gint failed;
} Job;

/* the blue-green-red ramp of the GIMP 2 JPEG Ghost plug-in */
static inline void
ramp (gfloat v, gfloat *rgb)
{
  v = CLAMP (v, 0.0f, 1.0f);
  if (v > 0.5f)
    {
      rgb[0] = (v - 0.5f) * 2.0f;
      rgb[1] = 1.0f - (v - 0.5f) * 2.0f;
      rgb[2] = 0.0f;
    }
  else
    {
      rgb[0] = 0.0f;
      rgb[1] = v * 2.0f;
      rgb[2] = 1.0f - v * 2.0f;
    }
}

static void
do_piece (Job  *job,
          gint  i)
{
  GeglRectangle need, area, piece;
  guint8       *orig, *copy;
  gfloat       *out;
  gint64       *sq, *sum;
  gdouble      *lo, *hi, *at;
  guint8       *arg;
  gsize         n_need, n_area;
  gint          k, x, y;

  need.x      = job->roi.x;
  need.width  = job->roi.width;
  need.y      = job->starts[i];
  need.height = job->starts[i + 1] - job->starts[i];
  area  = window_area (job->before, job->after, &job->canvas, &need);
  piece = fx_canvas_piece (&job->canvas, &area);

  n_need = (gsize) need.width * need.height;
  n_area = (gsize) area.width * area.height;
  orig = g_new (guint8, (gsize) piece.width * piece.height * 3);
  copy = g_new (guint8, (gsize) piece.width * piece.height * 3);
  /* sums of squared differences of the three channels, in whole levels,
   * and their integral image: exact, so that equal differences at two
   * qualities stay equal */
  sq   = g_new (gint64, n_area);
  sum  = g_new (gint64, (gsize) (area.width + 1) * (area.height + 1));
  lo   = g_new (gdouble, n_need);
  hi   = g_new (gdouble, n_need);
  at   = g_new (gdouble, n_need);
  arg  = g_new (guint8, n_need);
  fx_read_piece (job->input, job->format, &job->canvas, &piece, orig);

  for (k = 0; k < job->n_q; k++)
    {
      gsize ai;

      if (! fx_jpeg_round_trip (orig, piece.width, piece.height, job->q[k],
                                job->chroma, copy))
        {
          g_atomic_int_set (&job->failed, 1);
          memcpy (copy, orig, (gsize) piece.width * piece.height * 3);
        }
      /* the squared difference of the channels, in levels */
      for (y = 0, ai = 0; y < area.height; y++)
        {
          gsize         off = ((gsize) (area.y + y - piece.y) * piece.width +
                               (area.x - piece.x)) * 3;
          const guint8 *a   = orig + off;
          const guint8 *b   = copy + off;

          for (x = 0; x < area.width; x++, ai++, a += 3, b += 3)
            {
              gint d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];

              sq[ai] = d0 * d0 + d1 * d1 + d2 * d2;
            }
        }
      /* integral image: sum[(y + 1) * (w + 1) + x + 1] = sum of sq above
       * and left of (x, y) inclusive */
      memset (sum, 0, sizeof (gint64) * (area.width + 1));
      for (y = 0; y < area.height; y++)
        {
          gint64 *row  = sum + (gsize) (y + 1) * (area.width + 1);
          gint64 *prev = row - (area.width + 1);
          gint64  acc  = 0;

          row[0] = 0;
          for (x = 0; x < area.width; x++)
            {
              acc += sq[(gsize) y * area.width + x];
              row[x + 1] = prev[x + 1] + acc;
            }
        }
      for (y = 0; y < need.height; y++)
        for (x = 0; x < need.width; x++)
          {
            gint    px = need.x + x - area.x, py = need.y + y - area.y;
            gint    x1 = MAX (px - job->before, 0);
            gint    y1 = MAX (py - job->before, 0);
            gint    x2 = MIN (px + job->after + 1, area.width);
            gint    y2 = MIN (py + job->after + 1, area.height);
            gsize   w1 = area.width + 1;
            gint64  s  = sum[(gsize) y2 * w1 + x2] - sum[(gsize) y1 * w1 + x2] -
                         sum[(gsize) y2 * w1 + x1] + sum[(gsize) y1 * w1 + x1];
            /* equation 3: the mean over the channels and the window */
            gdouble v  = (gdouble) s / (3.0 * (x2 - x1) * (y2 - y1));
            gsize   o  = (gsize) y * need.width + x;

            if (k == 0 || v < lo[o])
              {
                lo[o]  = v;
                arg[o] = (guint8) k;
              }
            if (k == 0 || v > hi[o])
              hi[o] = v;
            if (k == job->shown)
              at[o] = v;
          }
    }

  out = g_new (gfloat, n_need * 4);
  gegl_buffer_get (job->input, &need, 1.0, job->format, out,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  for (k = 0; k < (gint) n_need; k++)
    {
      gfloat v, *p = out + (gsize) k * 4;

      switch (job->mode)
        {
        case FORENSICS_GHOST_NORMALIZED:
          v = hi[k] > lo[k] ? (gfloat) ((at[k] - lo[k]) / (hi[k] - lo[k])) : 0.0f;
          break;
        case FORENSICS_GHOST_DIFFERENCE:
          v = (gfloat) (sqrt (at[k]) / 255.0 * job->scale);
          break;
        default:
          v = job->q_hi > job->q_lo ?
              (gfloat) (job->q[arg[k]] - job->q_lo) / (job->q_hi - job->q_lo) : 0.0f;
          break;
        }
      if (job->colormap)
        ramp (v, p);
      else
        p[0] = p[1] = p[2] = MIN (v, 1e6f);
      if (! (p[3] == p[3]))
        p[3] = 0.0f;
    }
  gegl_buffer_set (job->output, &need, 0, job->format, out, GEGL_AUTO_ROWSTRIDE);

  g_free (out);
  g_free (arg);
  g_free (at);
  g_free (hi);
  g_free (lo);
  g_free (sum);
  g_free (sq);
  g_free (copy);
  g_free (orig);
}

static void
do_pieces (gsize    first,
           gsize    n,
           gpointer data)
{
  gsize i;

  for (i = first; i < first + n; i++)
    do_piece (data, (gint) i);
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
  gdouble         piece;

  (void) level;
  memset (&job, 0, sizeof job);
  job.input    = input;
  job.output   = output;
  job.format   = work_format (operation);
  job.chroma   = o->chroma;
  job.mode     = o->mode;
  job.colormap = o->colormap;
  job.scale    = (gfloat) o->scale;
  window_extent (o, &job.before, &job.after);
  job.n_q  = sweep_qualities (o, job.q, &job.shown);
  job.q_lo = job.q[0];
  job.q_hi = job.q[job.n_q - 1];
  canvas_for (operation, &job.canvas);
  if (! gegl_rectangle_intersect (&job.roi, result, &job.canvas.bbox))
    return TRUE;

  g_object_get (gegl_config (), "threads", &n_threads, NULL);
  n_pieces = fx_split_rows (&job.canvas, &job.roi, 2 * n_threads, &job.starts);
  piece    = (gdouble) job.roi.width * MIN (job.roi.height, 64) * job.n_q;
  gegl_parallel_distribute_range (n_pieces, 4096.0 / MAX (piece, 1.0),
                                  do_pieces, &job);
  g_free (job.starts);
  if (job.failed)
    g_warning ("forensics:jpeg-ghost: libjpeg failed; that part shows no difference");
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
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:jpeg-ghost",
    "title",           _("JPEG Ghost"),
    "categories",      "forensics:analysis",
    "description",     _("Saves the image again as a JPEG at a sweep of "
                         "qualities (Farid 2009). A part that was saved at "
                         "a lower quality before it was pasted in differs "
                         "least at that quality and shows up dark. An "
                         "indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("JPEG Ghost..."),
    NULL);
}

#endif
