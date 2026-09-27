/*
 * Median filtering detection, a GEGL operation
 *
 * median-detect.c
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
 * Traces of a median filter, block by block, after M. Kirchner and J.
 * Fridrich, "On detection of median filtering in digital images", Proc.
 * SPIE 7541, Media Forensics and Security II, 2010: a median filter
 * leaves "streaking", runs of equal neighbouring values, so that in its
 * output the first differences of the 8 bit values are 0 much more often
 * than +-1, even where the image has texture. The statistic of a block is
 * the ratio h0 / h1 of the counts of horizontal and vertical differences
 * 0 and +-1 inside it, counted in each of R, G and B (the paper works on
 * gray images; the gray of a color image filtered channel by channel
 * mixes three medians and hides the runs). Unfiltered textured image content gives about 0.5
 * (a difference of +-1 counts both signs); a 3 x 3 or 5 x 5 median about
 * 1 (tests/check-median-detect.c). Smooth blocks have many zero
 * differences anyway: blocks whose mean absolute difference is below the
 * texture threshold are not judged (shown blue).
 *
 * Sherloq's "Median Filtering" tool (Guido Bartoli,
 * github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/various/median.py) was read: it classifies 64 x 64
 * blocks with a 28 MB gradient boosted model (XGBoost) of image quality
 * metrics of repeated median filtering, trained on data it does not
 * describe; this operation uses the published statistic instead, and
 * keeps Sherloq's colors (blue: too flat, green: no sign, red: a sign of
 * median filtering) and its minimum variance idea.
 *
 * Reliable on images never saved as JPEG (PNG, TIFF, raw conversions);
 * JPEG compression, even at quality 90, hides the streaking and makes
 * smooth JPEG blocks look filtered: see the README. Written for this
 * operation. Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_median_mode)
  enum_value (FORENSICS_MEDIAN_MAP,   "map",   N_("Map (red: median filtered)"))
  enum_value (FORENSICS_MEDIAN_RATIO, "ratio", N_("Streaking ratio"))
enum_end (ForensicsMedianMode)

property_enum (mode, _("Show"), ForensicsMedianMode, forensics_median_mode,
               FORENSICS_MEDIAN_MAP)
  description (_("Red for blocks above the threshold, green below, blue too "
                 "flat to judge; or the ratio h0 / h1 as gray (ratio x "
                 "gain / 2)"))

property_int (block_size, _("Block size"), 32)
  description (_("The side of the blocks judged"))
  value_range (8, 256)

property_double (threshold, _("Threshold"), 0.8)
  description (_("The ratio of zero to +-1 differences above which a block "
                 "is marked (unfiltered texture: about 0.5; median "
                 "filtered: about 1)"))
  value_range (0.0, 10.0)
  ui_range (0.3, 2.0)

property_double (texture, _("Minimal texture"), 2.0)
  description (_("Blocks whose mean absolute difference between neighbours "
                 "is below this (in 8 bit levels) are not judged"))
  value_range (0.0, 50.0)
  ui_range (0.0, 10.0)

property_double (gain, _("Gain"), 1.0)
  description (_("For the ratio: gray = ratio x gain / 2"))
  value_range (0.0, 100.0)
  ui_range (0.1, 10.0)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     median_detect
#define GEGL_OP_C_SOURCE median-detect.c

#include "gegl-op.h"
#include "forensics-common.h"

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

/* the blocks that cover r, on the grid from the image's corner (for an
 * infinite image, from 0, 0) */
static GeglRectangle
blocks_of (GeglOperation *operation, const GeglRectangle *r)
{
  GeglRectangle bbox = fc_bbox (operation);
  gint          b    = GEGL_PROPERTIES (operation)->block_size;

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
  (void) input_pad;
  return blocks_of (operation, roi);
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  (void) input_pad;
  if (gegl_rectangle_is_infinite_plane (roi))
    return *roi;
  return blocks_of (operation, roi);
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
  GeglRectangle   roi, blk, read;
  gfloat         *pix, *planes[3], *out;
  gint            b      = CLAMP (o->block_size, 8, 256);
  gint            nbx, nby, bx, by, x, y, c;

  (void) level;
  if (inf)
    roi = *result;
  else if (! gegl_rectangle_intersect (&roi, result, &bbox))
    return TRUE;
  blk = blocks_of (operation, &roi);
  if (inf)
    {
      read = blk;
      pix = g_new (gfloat, (gsize) blk.width * blk.height * 4);
      gegl_buffer_get (input, &blk, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    }
  else
    pix = fc_read (input, format, &blk, &bbox, &read);
  for (c = 0; c < 3; c++)
    planes[c] = fc_plane (pix, &read, &read, &read, FC_RED + c, TRUE, FALSE);
  nbx = (read.width + b - 1) / b;
  nby = (read.height + b - 1) / b;
  out = g_new (gfloat, (gsize) roi.width * roi.height * 4);
  for (by = 0; by < nby; by++)
    for (bx = 0; bx < nbx; bx++)
      {
        gint    x0 = bx * b, y0 = by * b;
        gint    x1 = MIN (x0 + b, read.width), y1 = MIN (y0 + b, read.height);
        guint64 h0 = 0, h1 = 0, n = 0;
        gdouble sum = 0, ratio, texture;
        gfloat  rgb[3];

        for (c = 0; c < 3; c++)
          {
            const gfloat *plane = planes[c];

            for (y = y0; y < y1; y++)
              for (x = x0; x < x1; x++)
                {
                  gfloat v = plane[(gsize) y * read.width + x];

                  if (x + 1 < x1)
                    {
                      gfloat d = fabsf (plane[(gsize) y * read.width + x + 1] - v);

                      h0 += d == 0;
                      h1 += d == 1;
                      sum += d;
                      n++;
                    }
                  if (y + 1 < y1)
                    {
                      gfloat d = fabsf (plane[(gsize) (y + 1) * read.width + x] - v);

                      h0 += d == 0;
                      h1 += d == 1;
                      sum += d;
                      n++;
                    }
                }
          }
        ratio = (h0 + 1.0) / (h1 + 1.0);
        texture = n ? sum / n : 0;
        if (o->mode == FORENSICS_MEDIAN_RATIO)
          rgb[0] = rgb[1] = rgb[2] = (gfloat) (ratio * o->gain * 0.5);
        else if (texture < o->texture)
          {
            rgb[0] = rgb[1] = 0.0f;
            rgb[2] = 1.0f;
          }
        else if (ratio >= o->threshold)
          {
            rgb[0] = 1.0f;
            rgb[1] = rgb[2] = 0.0f;
          }
        else
          {
            rgb[1] = 1.0f;
            rgb[0] = rgb[2] = 0.0f;
          }
        for (y = MAX (read.y + y0, roi.y); y < MIN (read.y + y1, roi.y + roi.height); y++)
          for (x = MAX (read.x + x0, roi.x); x < MIN (read.x + x1, roi.x + roi.width); x++)
            {
              gfloat *p = out + ((gsize) (y - roi.y) * roi.width + x - roi.x) * 4;

              p[0] = rgb[0];
              p[1] = rgb[1];
              p[2] = rgb[2];
            }
      }
  fc_copy_alpha (pix, &read, &roi, out);
  gegl_buffer_set (output, &roi, 0, format, out, GEGL_AUTO_ROWSTRIDE);
  g_free (out);
  for (c = 0; c < 3; c++)
    g_free (planes[c]);
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
    "name",            "forensics:median-detect",
    "title",           _("Median Filtering Detection"),
    "categories",      "forensics:analysis",
    "description",     _("Marks blocks with the streaking a median filter "
                         "leaves (Kirchner and Fridrich 2010): red median "
                         "filtered, green not, blue too flat to tell. "
                         "Reliable only on images never saved as JPEG. An "
                         "indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Median Filtering Detection..."),
    NULL);
}

#endif
