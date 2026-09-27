/*
 * Min/Max deviation, a GEGL operation
 *
 * minmax.c
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
 * The pixels that are darker than all 8 of their neighbours (a local
 * minimum) or brighter than all of them (a local maximum), after the
 * "Min/Max Deviation" tool of Sherloq (Guido Bartoli,
 * github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/noise/minmax.py): minmax_dev () compares the
 * centre of each 3 x 3 patch of the 8 bit channel with the minimum and
 * maximum of the other eight, strictly. Sensor noise makes about one
 * pixel in nine a strict extremum each way; smooth areas, and areas that
 * were blurred, interpolated (resized, rotated), denoised or painted
 * with soft brushes, have few; clipped or flat areas none. A region
 * whose density of extrema differs from similar content elsewhere can be
 * from another source. Written for this operation after Sherloq's
 * method; Sherloq's "filter" (the standard deviation of the extrema map
 * in blocks) is replaced by their share in a window.
 *
 * Shown: the extrema (maxima red, minima green, as Sherloq's defaults,
 * on black), or their share in a square window around each pixel (red
 * the maxima, green the minima), times the gain. Pixels at the image's
 * edge are never extrema (their neighbours beyond it are copies of
 * themselves), as Sherloq leaves the border out. Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_minmax_channel)
  enum_value (FORENSICS_MINMAX_LUMA,  "luminance", N_("Luminance"))
  enum_value (FORENSICS_MINMAX_RED,   "red",       N_("Red"))
  enum_value (FORENSICS_MINMAX_GREEN, "green",     N_("Green"))
  enum_value (FORENSICS_MINMAX_BLUE,  "blue",      N_("Blue"))
  enum_value (FORENSICS_MINMAX_NORM,  "rgb-norm",  N_("RGB norm"))
enum_end (ForensicsMinmaxChannel)

enum_start (forensics_minmax_mode)
  enum_value (FORENSICS_MINMAX_MARKERS, "markers", N_("Extrema"))
  enum_value (FORENSICS_MINMAX_DENSITY, "density", N_("Density of extrema"))
enum_end (ForensicsMinmaxMode)

property_enum (channel, _("Channel"), ForensicsMinmaxChannel,
               forensics_minmax_channel, FORENSICS_MINMAX_LUMA)
  description (_("The 8 bit values compared"))

property_enum (mode, _("Show"), ForensicsMinmaxMode, forensics_minmax_mode,
               FORENSICS_MINMAX_MARKERS)
  description (_("The pixels that are local extrema, or their share in a "
                 "window around each pixel"))

property_int (window, _("Window"), 15)
  description (_("The side of the window for the density"))
  value_range (3, 101)

property_double (gain, _("Gain"), 4.0)
  description (_("How much the density is amplified (noise makes about "
                 "1 pixel in 9 an extremum each way)"))
  value_range (0.0, 100.0)
  ui_range (0.5, 20.0)

#else

#define GEGL_OP_AREA_FILTER
#define GEGL_OP_NAME     minmax
#define GEGL_OP_C_SOURCE minmax.c

#include "gegl-op.h"
#include "forensics-common.h"

static void
extent (GeglProperties *o, gint *before, gint *after)
{
  gint w = o->mode == FORENSICS_MINMAX_DENSITY ? MAX (o->window, 1) : 1;

  *before = (w - 1) / 2;
  *after  = w - 1 - *before;
}

static void
prepare (GeglOperation *operation)
{
  GeglOperationAreaFilter *area = GEGL_OPERATION_AREA_FILTER (operation);
  GeglProperties          *o    = GEGL_PROPERTIES (operation);
  const Babl              *format = fc_format (operation);
  gint                     before, after;

  extent (o, &before, &after);
  area->left = area->top = before + 1;
  area->right = area->bottom = after + 1;
  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  return fc_bbox (operation);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *format = fc_format (operation);
  GeglRectangle   bbox   = fc_bbox (operation);
  GeglRectangle   ext, src, read, flags_r;
  gfloat         *pix, *plane, *out;
  gint8          *flag;
  gint            before, after, x, y;
  gboolean        density = o->mode == FORENSICS_MINMAX_DENSITY;

  (void) level;
  extent (o, &before, &after);
  /* the flags are needed on roi grown by the window, inside the image */
  ext = *roi;
  ext.x -= before;
  ext.y -= before;
  ext.width += before + after;
  ext.height += before + after;
  if (gegl_rectangle_is_infinite_plane (&bbox))
    flags_r = ext;
  else if (! gegl_rectangle_intersect (&flags_r, &ext, &bbox))
    return TRUE;
  src = fc_grow (&flags_r, 1);
  {
    GeglRectangle want = src;

    if (gegl_rectangle_is_infinite_plane (&bbox))
      {
        pix = g_new (gfloat, (gsize) want.width * want.height * 4);
        gegl_buffer_get (input, &want, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE,
                         GEGL_ABYSS_NONE);
        read = want;
        plane = fc_plane (pix, &read, &src, &want, o->channel, TRUE, FALSE);
      }
    else
      {
        pix = fc_read (input, format, &want, &bbox, &read);
        plane = fc_plane (pix, &read, &src, &bbox, o->channel, TRUE, FALSE);
      }
  }
  flag = g_new0 (gint8, (gsize) flags_r.width * flags_r.height);
  for (y = 0; y < flags_r.height; y++)
    for (x = 0; x < flags_r.width; x++)
      {
        const gfloat *c = plane + (gsize) (y + 1) * src.width + x + 1;
        gfloat        v = c[0], lo = G_MAXFLOAT, hi = -G_MAXFLOAT;
        gint          i, j;

        for (j = -1; j <= 1; j++)
          for (i = -1; i <= 1; i++)
            if (i || j)
              {
                gfloat u = c[j * src.width + i];

                lo = MIN (lo, u);
                hi = MAX (hi, u);
              }
        flag[(gsize) y * flags_r.width + x] = v < lo ? -1 : v > hi ? 1 : 0;
      }

  out = g_new (gfloat, (gsize) roi->width * roi->height * 4);
  if (! density)
    {
      for (y = 0; y < roi->height; y++)
        for (x = 0; x < roi->width; x++)
          {
            gfloat *p = out + ((gsize) y * roi->width + x) * 4;
            gint    fx = roi->x + x - flags_r.x, fy = roi->y + y - flags_r.y;
            gint    f = 0;

            if (fx >= 0 && fy >= 0 && fx < flags_r.width && fy < flags_r.height)
              f = flag[(gsize) fy * flags_r.width + fx];
            p[0] = f > 0 ? 1.0f : 0.0f;
            p[1] = f < 0 ? 1.0f : 0.0f;
            p[2] = 0.0f;
            p[3] = 1.0f;
          }
    }
  else
    {
      /* integral images of the maxima and the minima */
      gint     iw = flags_r.width + 1;
      guint32 *smax = g_new0 (guint32, (gsize) iw * (flags_r.height + 1));
      guint32 *smin = g_new0 (guint32, (gsize) iw * (flags_r.height + 1));
      gfloat   gain = (gfloat) o->gain;

      for (y = 0; y < flags_r.height; y++)
        {
          guint32 amax = 0, amin = 0;

          for (x = 0; x < flags_r.width; x++)
            {
              gint8 f = flag[(gsize) y * flags_r.width + x];

              amax += f > 0;
              amin += f < 0;
              smax[(gsize) (y + 1) * iw + x + 1] = smax[(gsize) y * iw + x + 1] + amax;
              smin[(gsize) (y + 1) * iw + x + 1] = smin[(gsize) y * iw + x + 1] + amin;
            }
        }
      for (y = 0; y < roi->height; y++)
        for (x = 0; x < roi->width; x++)
          {
            gfloat *p = out + ((gsize) y * roi->width + x) * 4;
            gint    gx = roi->x + x - flags_r.x, gy = roi->y + y - flags_r.y;
            gint    x1 = CLAMP (gx - before, 0, flags_r.width);
            gint    y1 = CLAMP (gy - before, 0, flags_r.height);
            gint    x2 = CLAMP (gx + after + 1, 0, flags_r.width);
            gint    y2 = CLAMP (gy + after + 1, 0, flags_r.height);
            gdouble n = (gdouble) (x2 - x1) * (y2 - y1);
            guint32 cmax, cmin;

            if (n <= 0)
              {
                p[0] = p[1] = p[2] = 0.0f;
                p[3] = 1.0f;
                continue;
              }
            cmax = smax[(gsize) y2 * iw + x2] - smax[(gsize) y1 * iw + x2] -
                   smax[(gsize) y2 * iw + x1] + smax[(gsize) y1 * iw + x1];
            cmin = smin[(gsize) y2 * iw + x2] - smin[(gsize) y1 * iw + x2] -
                   smin[(gsize) y2 * iw + x1] + smin[(gsize) y1 * iw + x1];
            p[0] = (gfloat) (cmax / n) * gain;
            p[1] = (gfloat) (cmin / n) * gain;
            p[2] = 0.0f;
            p[3] = 1.0f;
          }
      g_free (smax);
      g_free (smin);
    }
  if (pix)
    {
      GeglRectangle a;

      if (gegl_rectangle_intersect (&a, roi, &read))
        {
          /* alpha where the pixels were read (roi lies inside the image) */
          for (y = 0; y < a.height; y++)
            for (x = 0; x < a.width; x++)
              {
                gfloat v = pix[((gsize) (a.y + y - read.y) * read.width + a.x + x - read.x) * 4 + 3];

                out[((gsize) (a.y + y - roi->y) * roi->width + a.x + x - roi->x) * 4 + 3] =
                  v == v ? v : 0.0f;
              }
        }
    }
  gegl_buffer_set (output, roi, 0, format, out, GEGL_AUTO_ROWSTRIDE);
  g_free (out);
  g_free (flag);
  g_free (plane);
  g_free (pix);
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare          = prepare;
  operation_class->get_bounding_box = get_bounding_box;
  operation_class->opencl_support   = FALSE;
  filter_class->process             = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:minmax",
    "title",           _("Min/Max Deviation"),
    "categories",      "forensics:analysis",
    "description",     _("Shows the pixels darker or brighter than all their "
                         "neighbours (after Sherloq), or their density: "
                         "noise makes many, smoothing, interpolation and "
                         "painting few. A region with another density than "
                         "similar content can have another origin. An "
                         "indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Min/Max Deviation..."),
    NULL);
}

#endif
