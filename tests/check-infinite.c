/*
 * Every operation on an infinite input
 *
 * check-infinite.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-infinite <module.so ...>
 *
 * gegl:color and other sources have no edges. The operations that need
 * the whole image (clone detection, principal components, auto levels)
 * pass such an input through; the others work on the part asked for.
 * Either way: no crash, finite values, in a few seconds.
 */

#include "check-common.h"

gint
main (gint    argc,
      gchar **argv)
{
  static const struct { const gchar *op, *prop; gboolean passes; } cases[] = {
    { "forensics:error-level", NULL, FALSE },
    { "forensics:error-level", "auto-levels", TRUE },
    { "forensics:jpeg-ghost", NULL, FALSE },
    { "forensics:noise", NULL, FALSE },
    { "forensics:noise", "auto-levels", TRUE },
    { "forensics:luminance-gradient", NULL, FALSE },
    { "forensics:clone-detect", NULL, TRUE },
    { "forensics:pca", NULL, TRUE },
  };
  gint i;

  if (! check_start (&argc, &argv, NULL))
    return 2;
  for (i = 0; i < (gint) G_N_ELEMENTS (cases); i++)
    {
      GeglRectangle r = { -20, 7, 64, 48 };
      GeglNode     *g, *src, *node;
      gfloat       *out, *in;
      gsize         k;
      gboolean      finite = TRUE, same = TRUE;
      gint64        t0 = g_get_monotonic_time ();
      gchar        *name;

      if (! gegl_has_operation (cases[i].op))
        continue;
      g    = gegl_node_new ();
      src  = gegl_node_new_child (g, "operation", "gegl:color", NULL);
      {
        GeglColor *color = gegl_color_new ("rgba(0.2, 0.4, 0.6, 1.0)");

        gegl_node_set (src, "value", color, NULL);
        g_object_unref (color);
      }
      node = gegl_node_new_child (g, "operation", cases[i].op, NULL);
      if (cases[i].prop)
        gegl_node_set (node, cases[i].prop, TRUE, NULL);
      gegl_node_link (src, node);
      out = g_new0 (gfloat, (gsize) r.width * r.height * 4);
      in  = g_new0 (gfloat, (gsize) r.width * r.height * 4);
      gegl_node_blit (node, 1.0, &r, babl_format (WORK), out, GEGL_AUTO_ROWSTRIDE,
                      GEGL_BLIT_DEFAULT);
      gegl_node_blit (src, 1.0, &r, babl_format (WORK), in, GEGL_AUTO_ROWSTRIDE,
                      GEGL_BLIT_DEFAULT);
      for (k = 0; k < (gsize) r.width * r.height * 4; k++)
        {
          if (! isfinite (out[k]))
            finite = FALSE;
          if (cases[i].passes && fabsf (out[k] - in[k]) > 1e-6f)
            same = FALSE;
        }
      g_free (in);
      name = g_strdup_printf ("infinite_input_%s%s%s", cases[i].op + strlen ("forensics:"),
                              cases[i].prop ? "_" : "", cases[i].prop ? cases[i].prop : "");
      report (name, finite && same && (g_get_monotonic_time () - t0) < 5000000,
              cases[i].passes ? "passed through" : "worked on the part asked for");
      g_free (name);
      g_free (out);
      g_object_unref (g);
    }
  return check_end ();
}
