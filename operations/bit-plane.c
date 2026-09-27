/*
 * Bit planes, a GEGL operation
 *
 * bit-plane.c
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
 * One bit of the 8 bit value of a channel, as black and white: the "Bit
 * Planes Values" tool of Sherloq (Guido Bartoli,
 * github.com/GuidoBartoli/sherloq, GPL-3.0,
 * gui/sherloq_app/tools/noise/planes.py), which shows value & 2^b. The
 * low planes of a photograph look like noise; a region pasted from
 * another picture, painted, or with other processing can have other
 * noise there, or structure where the rest has none (and a hidden
 * message, steganography, can show in plane 0). Written for this
 * operation after Sherloq's method.
 *
 * The channel: luma (OpenCV's gray, as Sherloq), red, green, blue, or the
 * length of R, G, B over the square root of 3 (Sherloq's "RGB Norm"
 * without the / sqrt 3 overflows 8 bit and wraps around; here it stays in
 * range). White where the bit is 1. Sherloq's optional median or Gaussian
 * smoothing of the plane is left to GIMP's own filters. Alpha is passed
 * through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_bit_plane_channel)
  enum_value (FORENSICS_BIT_PLANE_LUMA,  "luminance", N_("Luminance"))
  enum_value (FORENSICS_BIT_PLANE_RED,   "red",       N_("Red"))
  enum_value (FORENSICS_BIT_PLANE_GREEN, "green",     N_("Green"))
  enum_value (FORENSICS_BIT_PLANE_BLUE,  "blue",      N_("Blue"))
  enum_value (FORENSICS_BIT_PLANE_NORM,  "rgb-norm",  N_("RGB norm"))
enum_end (ForensicsBitPlaneChannel)

property_enum (channel, _("Channel"), ForensicsBitPlaneChannel,
               forensics_bit_plane_channel, FORENSICS_BIT_PLANE_LUMA)
  description (_("The 8 bit values whose bit is shown"))

property_int (bit, _("Bit"), 0)
  description (_("Which bit: 0 is the lowest (the finest noise), 7 the "
                 "highest"))
  value_range (0, 7)

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     bit_plane
#define GEGL_OP_C_SOURCE bit-plane.c

#include "gegl-op.h"
#include "forensics-common.h"

static void
prepare (GeglOperation *operation)
{
  const Babl *format = fc_format (operation);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static gboolean
process (GeglOperation       *operation,
         void                *in_buf,
         void                *out_buf,
         glong                n,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o   = GEGL_PROPERTIES (operation);
  const gfloat   *in  = in_buf;
  gfloat         *out = out_buf;
  gint            bit = CLAMP (o->bit, 0, 7);
  glong           i;

  (void) roi;
  (void) level;
  for (i = 0; i < n; i++, in += 4, out += 4)
    {
      gint   v = (gint) fc_channel (in, o->channel, TRUE);
      gfloat a = in[3];

      out[0] = out[1] = out[2] = (v >> bit) & 1 ? 1.0f : 0.0f;
      out[3] = a == a ? a : 0.0f;
    }
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationPointFilterClass *point_class     = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  operation_class->prepare        = prepare;
  operation_class->opencl_support = FALSE;
  point_class->process            = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:bit-plane",
    "title",           _("Bit Planes"),
    "categories",      "forensics:analysis",
    "description",     _("Shows one bit of the 8 bit values of a channel "
                         "(after Sherloq): the low bits of a photograph look "
                         "like noise; a region with other noise or with "
                         "structure there can stand out. An indicator, not "
                         "proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Bit Planes..."),
    NULL);
}

#endif
