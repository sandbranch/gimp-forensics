/*
 * Principal component analysis of the colors, a GEGL operation
 *
 * pca.c
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
 * The colors of an image (its encoded R'G'B' values) mostly vary along
 * one direction in color space (brightness), less along a second and
 * little along a third. Principal component analysis finds these
 * directions: the eigenvectors of the covariance of all colors. Viewing
 * the image along the second or third one hides what most of the image
 * shares and shows what differs in its color relations: a region pasted
 * from another picture, retouched or recolored, and compression
 * artifacts (Krawetz, "A Picture's Worth", 2007; Forensically's PCA
 * tool). Written for this operation; the 3 x 3 eigenproblem is solved
 * with Jacobi rotations.
 *
 * Shown, for component k (1 to 3, by falling variance): the projection
 * of each color onto it around gray (0.5 +- 2 standard deviations of the
 * component at scale 1), or the distance of each color from the line
 * through the mean along it (0 to 4 standard deviations of the other two
 * components at scale 1). Needs the whole image. Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_pca_mode)
  enum_value (FORENSICS_PCA_PROJECTION, "projection", N_("Projection"))
  enum_value (FORENSICS_PCA_DISTANCE,   "distance",   N_("Distance from the component"))
enum_end (ForensicsPcaMode)

property_int (component, _("Component"), 2)
  description (_("Which principal component: 1 has the most variance "
                 "(mostly the brightness), 3 the least"))
  value_range (1, 3)

property_enum (mode, _("Show"), ForensicsPcaMode, forensics_pca_mode,
               FORENSICS_PCA_PROJECTION)
  description (_("The colors projected onto the component, or their "
                 "distance from it"))

property_double (scale, _("Scale"), 1.0)
  description (_("How strongly the values are shown"))
  value_range (0.0, 100.0)
  ui_range (0.25, 8.0)

property_boolean (invert, _("Invert"), FALSE)
  description (_("Show the values inverted"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     pca
#define GEGL_OP_C_SOURCE pca.c

#include "gegl-op.h"
#include <math.h>
#include <string.h>

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

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle r = get_bounding_box (operation);

  (void) input_pad;
  return gegl_rectangle_is_infinite_plane (&r) ? *roi : r;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  (void) input_pad;
  (void) roi;
  return get_bounding_box (operation);
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  GeglRectangle r = get_bounding_box (operation);

  return gegl_rectangle_is_infinite_plane (&r) ? *roi : r;
}

static inline gdouble
sane (gfloat v)
{
  if (v != v)
    return 0.0;
  if (v > 1e6f)
    return 1.0;
  if (v < -1e6f)
    return 0.0;
  return v;
}

/* the eigenvalues (d, falling) and eigenvectors (the columns of v) of
 * the symmetric 3 x 3 matrix a, by cyclic Jacobi rotations */
static void
eigen3 (gdouble a[3][3], gdouble d[3], gdouble v[3][3])
{
  gint i, j, k, sweep;

  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
      v[i][j] = i == j;
  for (sweep = 0; sweep < 50; sweep++)
    {
      gdouble off = fabs (a[0][1]) + fabs (a[0][2]) + fabs (a[1][2]);

      if (off < 1e-30)
        break;
      for (i = 0; i < 2; i++)
        for (j = i + 1; j < 3; j++)
          {
            gdouble theta, t, c, s;

            if (fabs (a[i][j]) < 1e-300)
              continue;
            theta = (a[j][j] - a[i][i]) / (2 * a[i][j]);
            t = (theta >= 0 ? 1 : -1) / (fabs (theta) + sqrt (theta * theta + 1));
            c = 1 / sqrt (t * t + 1);
            s = t * c;
            for (k = 0; k < 3; k++)
              {
                gdouble aki = a[k][i], akj = a[k][j];

                a[k][i] = c * aki - s * akj;
                a[k][j] = s * aki + c * akj;
              }
            for (k = 0; k < 3; k++)
              {
                gdouble aik = a[i][k], ajk = a[j][k];

                a[i][k] = c * aik - s * ajk;
                a[j][k] = s * aik + c * ajk;
              }
            for (k = 0; k < 3; k++)
              {
                gdouble vki = v[k][i], vkj = v[k][j];

                v[k][i] = c * vki - s * vkj;
                v[k][j] = s * vki + c * vkj;
              }
          }
    }
  for (i = 0; i < 3; i++)
    d[i] = a[i][i];
  /* by falling eigenvalue; each vector with its largest entry positive,
   * so that the result does not flip between runs or images */
  for (i = 0; i < 2; i++)
    for (j = i + 1; j < 3; j++)
      if (d[j] > d[i])
        {
          gdouble t = d[i];

          d[i] = d[j];
          d[j] = t;
          for (k = 0; k < 3; k++)
            {
              t = v[k][i];
              v[k][i] = v[k][j];
              v[k][j] = t;
            }
        }
  for (j = 0; j < 3; j++)
    {
      gint big = 0;

      for (k = 1; k < 3; k++)
        if (fabs (v[k][j]) > fabs (v[big][j]))
          big = k;
      if (v[big][j] < 0)
        for (k = 0; k < 3; k++)
          v[k][j] = -v[k][j];
    }
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *format = work_format (operation);
  GeglRectangle   bbox   = get_bounding_box (operation);
  gsize           n, i;
  gfloat         *pix;
  gdouble         mean[3] = { 0, 0, 0 }, cov[3][3], d[3], v[3][3], e[3];
  gdouble         gain;
  gint            c, r, k = CLAMP (o->component, 1, 3) - 1;

  (void) level;
  if (gegl_rectangle_is_infinite_plane (&bbox) || bbox.width <= 0 || bbox.height <= 0)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }
  n   = (gsize) bbox.width * bbox.height;
  pix = g_new (gfloat, n * 4);
  gegl_buffer_get (input, &bbox, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);

  memset (cov, 0, sizeof cov);
  for (i = 0; i < n; i++)
    for (c = 0; c < 3; c++)
      mean[c] += sane (pix[4 * i + c]);
  for (c = 0; c < 3; c++)
    mean[c] /= n;
  for (i = 0; i < n; i++)
    {
      gdouble x[3];

      for (c = 0; c < 3; c++)
        x[c] = sane (pix[4 * i + c]) - mean[c];
      for (c = 0; c < 3; c++)
        for (r = 0; r < 3; r++)
          cov[c][r] += x[c] * x[r];
    }
  for (c = 0; c < 3; c++)
    for (r = 0; r < 3; r++)
      cov[c][r] /= n;
  eigen3 (cov, d, v);
  for (c = 0; c < 3; c++)
    e[c] = v[c][k];

  {
    /* +- 2 standard deviations to 0 and 1: of the component, or of the
     * other two for the distance; at least a thousandth of the image's
     * own, so that colors all on one line do not blow up rounding */
    gdouble total = MAX (d[0] + d[1] + d[2], 0.0);
    gdouble var   = o->mode == FORENSICS_PCA_PROJECTION ? d[k] : total - d[k];
    gdouble sd    = sqrt (MAX (var, 1e-6 * total));

    gain = sd > 1e-12 ? o->scale / (4 * sd) : 0.0;
  }

  for (i = 0; i < n; i++)
    {
      gfloat *p = pix + 4 * i;
      gdouble x[3], t = 0, out;

      for (c = 0; c < 3; c++)
        {
          x[c] = sane (p[c]) - mean[c];
          t += x[c] * e[c];
        }
      if (o->mode == FORENSICS_PCA_PROJECTION)
        out = 0.5 + t * gain;
      else
        {
          gdouble q = 0;

          for (c = 0; c < 3; c++)
            q += (x[c] - t * e[c]) * (x[c] - t * e[c]);
          out = sqrt (q) * gain;
        }
      if (o->invert)
        out = 1.0 - out;
      p[0] = p[1] = p[2] = (gfloat) out;
      if (! (p[3] == p[3]))
        p[3] = 0.0f;
    }

  gegl_buffer_set (output, &bbox, 0, format, pix, GEGL_AUTO_ROWSTRIDE);
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
  operation_class->get_cached_region         = get_cached_region;
  operation_class->opencl_support            = FALSE;
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:pca",
    "title",           _("Principal Components"),
    "categories",      "forensics:analysis",
    "description",     _("Shows the image along a principal component of its "
                         "colors. The weaker components hide what most of "
                         "the image shares and can show a region whose "
                         "colors relate differently. An indicator, not "
                         "proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Principal Components..."),
    NULL);
}

#endif
