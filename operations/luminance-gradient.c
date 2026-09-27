/*
 * Luminance gradient, a GEGL operation
 *
 * luminance-gradient.c
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
 * How the brightness changes along x and y, as a color, after the
 * Luminance Gradient of Forensically (Jonas Wagner): parts of a picture
 * that face the light the same way, under the same light, get similar
 * colors; an object pasted from a picture with other light, or edges much
 * sharper or softer than the rest, stand out. Written for this operation.
 *
 * The luma Y' (Rec. 709 weights on the image's encoded values) is
 * differentiated with central differences, g = (Y'(x + 1) - Y'(x - 1)) / 2
 * per pixel, the edge pixels repeated beyond the image. Shown as:
 *
 *   - a normal map: n = normalize (-gx k, -gy k, 1), colors 0.5 + n / 2
 *     (flat is light blue, 0.5 0.5 1; red and green grow where the image
 *     gets darker to the right and down), with k the intensity (8 gives
 *     Forensically's default slope: 2 levels of color per level of Y');
 *   - direction: the direction in which it gets brighter as a hue (red
 *     to the right, yellow-green up, cyan to the left, violet down), its
 *     size x k / 4 as the value;
 *   - magnitude: |g| x k / 4 as gray.
 *
 * Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_gradient_mode)
  enum_value (FORENSICS_GRADIENT_NORMAL,    "normal",    N_("Normal map"))
  enum_value (FORENSICS_GRADIENT_DIRECTION, "direction", N_("Direction as hue"))
  enum_value (FORENSICS_GRADIENT_MAGNITUDE, "magnitude", N_("Magnitude"))
enum_end (ForensicsGradientMode)

property_enum (mode, _("Show"), ForensicsGradientMode, forensics_gradient_mode,
               FORENSICS_GRADIENT_NORMAL)
  description (_("The gradient as the colors of a normal map (Forensically's "
                 "view), its direction as a hue with its size as the "
                 "brightness, or its size alone"))

property_double (intensity, _("Intensity"), 8.0)
  description (_("How strongly the gradient is shown"))
  value_range (0.0, 1000.0)
  ui_range (0.5, 64.0)
  ui_gamma (2.0)

#else

#define GEGL_OP_AREA_FILTER
#define GEGL_OP_NAME     luminance_gradient
#define GEGL_OP_C_SOURCE luminance-gradient.c

#include "gegl-op.h"
#include <math.h>

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
  GeglOperationAreaFilter *area   = GEGL_OPERATION_AREA_FILTER (operation);
  const Babl              *format = work_format (operation);

  area->left = area->right = area->top = area->bottom = 1;
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

/* h in turns (0 to 1), s = 1, v: R'G'B' */
static inline void
hue_value (gfloat h, gfloat v, gfloat *rgb)
{
  gfloat h6 = (h - floorf (h)) * 6.0f;
  gint   i  = (gint) h6 % 6;
  gfloat f  = h6 - floorf (h6);

  switch (i)
    {
    case 0:  rgb[0] = v;           rgb[1] = v * f;         rgb[2] = 0;           break;
    case 1:  rgb[0] = v * (1 - f); rgb[1] = v;             rgb[2] = 0;           break;
    case 2:  rgb[0] = 0;           rgb[1] = v;             rgb[2] = v * f;       break;
    case 3:  rgb[0] = 0;           rgb[1] = v * (1 - f);   rgb[2] = v;           break;
    case 4:  rgb[0] = v * f;       rgb[1] = 0;             rgb[2] = v;           break;
    default: rgb[0] = v;           rgb[1] = 0;             rgb[2] = v * (1 - f); break;
    }
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *format = work_format (operation);
  GeglRectangle   bbox   = get_bounding_box (operation);
  GeglRectangle   src    = { roi->x - 1, roi->y - 1, roi->width + 2, roi->height + 2 };
  GeglRectangle   read;
  gfloat         *in, *y_, *out;
  gfloat          k      = (gfloat) o->intensity;
  gint            x, y;

  (void) level;
  /* beyond the image: the edge pixels (clamped to the image here, not to
   * the input buffer, which GEGL may cut to the region it needs) */
  if (! gegl_rectangle_intersect (&read, &src, &bbox))
    return TRUE;
  in  = g_new (gfloat, (gsize) read.width * read.height * 4);
  y_  = g_new (gfloat, (gsize) src.width * src.height);
  out = g_new (gfloat, (gsize) roi->width * roi->height * 4);
  gegl_buffer_get (input, &read, 1.0, format, in, GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);
  for (y = 0; y < src.height; y++)
    for (x = 0; x < src.width; x++)
      {
        gint          sx = CLAMP (src.x + x, read.x, read.x + read.width - 1) - read.x;
        gint          sy = CLAMP (src.y + y, read.y, read.y + read.height - 1) - read.y;
        const gfloat *p  = in + ((gsize) sy * read.width + sx) * 4;

        y_[(gsize) y * src.width + x] = 0.2126f * sane (p[0]) +
                                        0.7152f * sane (p[1]) +
                                        0.0722f * sane (p[2]);
      }

  for (y = 0; y < roi->height; y++)
    for (x = 0; x < roi->width; x++)
      {
        const gfloat *c  = y_ + (gsize) (y + 1) * src.width + x + 1;
        gfloat        gx = (c[1] - c[-1]) * 0.5f;
        gfloat        gy = (c[src.width] - c[-src.width]) * 0.5f;
        gfloat       *p  = out + ((gsize) y * roi->width + x) * 4;
        gfloat        a  = in[((gsize) (roi->y + y - read.y) * read.width +
                              roi->x + x - read.x) * 4 + 3];

        switch (o->mode)
          {
          case FORENSICS_GRADIENT_NORMAL:
            {
              gfloat nx = -gx * k, ny = -gy * k;
              gfloat n  = 1.0f / sqrtf (nx * nx + ny * ny + 1.0f);

              p[0] = 0.5f + 0.5f * nx * n;
              p[1] = 0.5f + 0.5f * ny * n;
              p[2] = 0.5f + 0.5f * n;
            }
            break;
          case FORENSICS_GRADIENT_DIRECTION:
            {
              /* the angle with y up (the screen's y is down) */
              gfloat h = atan2f (-gy, gx) / (2.0f * (gfloat) G_PI);

              hue_value (h < 0 ? h + 1.0f : h,
                         MIN (sqrtf (gx * gx + gy * gy) * k * 0.25f, 1.0f), p);
            }
            break;
          default:
            p[0] = p[1] = p[2] = sqrtf (gx * gx + gy * gy) * k * 0.25f;
            break;
          }
        p[3] = a == a ? a : 0.0f;
      }

  gegl_buffer_set (output, roi, 0, format, out, GEGL_AUTO_ROWSTRIDE);
  g_free (out);
  g_free (y_);
  g_free (in);
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
    "name",            "forensics:luminance-gradient",
    "title",           _("Luminance Gradient"),
    "categories",      "forensics:analysis",
    "description",     _("Shows how the brightness changes, as a color. "
                         "Surfaces facing the light the same way get "
                         "similar colors; an object lit from elsewhere, or "
                         "edges sharper or softer than the rest, can stand "
                         "out. An indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Luminance Gradient..."),
    NULL);
}

#endif
