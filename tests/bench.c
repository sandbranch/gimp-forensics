/*
 * How long the forensics operations take on a large image
 *
 * bench.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   bench <module.so ...> [--size WxH] [--threads N] [--op NAME [prop=value ...]]...
 *
 * Makes a photo-like scene of the size (6000 x 4000 by default), then
 * renders each operation with its settings into a float buffer, as GIMP
 * renders a filter's result, three times, and prints the fastest time.
 * Without --op every operation of the modules runs with its defaults.
 */

#include "check-common.h"
#include <gegl-plugin.h>

static void
set_prop (GeglNode *node, const gchar *op, const gchar *assignment)
{
  gchar      **kv = g_strsplit (assignment, "=", 2);
  GParamSpec  *spec;
  GValue       v = G_VALUE_INIT;

  spec = kv[0] && kv[1] ? gegl_operation_find_property (op, kv[0]) : NULL;
  if (! spec)
    g_error ("no property %s of %s", assignment, op);
  g_value_init (&v, spec->value_type);
  if (G_IS_PARAM_SPEC_INT (spec))
    g_value_set_int (&v, atoi (kv[1]));
  else if (G_IS_PARAM_SPEC_DOUBLE (spec))
    g_value_set_double (&v, g_ascii_strtod (kv[1], NULL));
  else if (G_IS_PARAM_SPEC_BOOLEAN (spec))
    g_value_set_boolean (&v, ! strcmp (kv[1], "true") || ! strcmp (kv[1], "1"));
  else if (G_IS_PARAM_SPEC_ENUM (spec))
    {
      GEnumClass *ec = g_type_class_ref (spec->value_type);
      GEnumValue *ev = g_enum_get_value_by_nick (ec, kv[1]);

      if (! ev)
        g_error ("no value %s of %s", kv[1], kv[0]);
      g_value_set_enum (&v, ev->value);
      g_type_class_unref (ec);
    }
  else
    g_error ("cannot set %s", kv[0]);
  gegl_node_set_property (node, kv[0], &v);
  g_value_unset (&v);
  g_strfreev (kv);
}

static void
bench (GeglBuffer *in, const gchar *op, gchar **props, gint n_props)
{
  const GeglRectangle *r = gegl_buffer_get_extent (in);
  gdouble              best = G_MAXDOUBLE;
  gint                 k, i;
  GString             *what = g_string_new (op);

  for (i = 0; i < n_props; i++)
    g_string_append_printf (what, " %s", props[i]);
  for (k = 0; k < 3; k++)
    {
      GeglNode   *graph = gegl_node_new ();
      GeglNode   *src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                                               "buffer", in, NULL);
      GeglNode   *node  = gegl_node_new_child (graph, "operation", op, NULL);
      GeglBuffer *out   = gegl_buffer_new (r, babl_format (WORK));
      GeglNode   *sink  = gegl_node_new_child (graph, "operation", "gegl:write-buffer",
                                               "buffer", out, NULL);
      gint64      t0;

      for (i = 0; i < n_props; i++)
        set_prop (node, op, props[i]);
      gegl_node_link_many (src, node, sink, NULL);
      t0 = g_get_monotonic_time ();
      gegl_node_process (sink);
      best = MIN (best, (g_get_monotonic_time () - t0) / 1e6);
      g_object_unref (out);
      g_object_unref (graph);
    }
  printf ("%-60s %8.3f s\n", what->str, best);
  fflush (stdout);
  g_string_free (what, TRUE);
}

gint
main (gint argc, gchar **argv)
{
  gint        w = 6000, h = 4000, threads = 0, i, n_mod = 1;
  gfloat     *pixels;
  GeglBuffer *in;
  gchar     **ops;

  while (n_mod < argc && g_str_has_suffix (argv[n_mod], ".so"))
    n_mod++;
  {
    gint mod_argc = n_mod;

    if (! check_start (&mod_argc, &argv, NULL))
      return 2;
  }
  for (i = n_mod; i < argc; i++)
    if (! strcmp (argv[i], "--size") && i + 1 < argc)
      sscanf (argv[++i], "%dx%d", &w, &h);
    else if (! strcmp (argv[i], "--threads") && i + 1 < argc)
      threads = atoi (argv[++i]);
  if (threads > 0)
    g_object_set (gegl_config (), "threads", threads, NULL);
  g_object_get (gegl_config (), "threads", &threads, NULL);

  printf ("%d x %d (%.1f megapixels), %d threads\n", w, h, w * (gdouble) h / 1e6,
          threads);
  pixels = make_scene (w, h, 1, 3.0);
  in = buffer_from (pixels, w, h, WORK);
  g_free (pixels);

  ops = NULL;
  for (i = n_mod; i < argc; i++)
    if (! strcmp (argv[i], "--op") && i + 1 < argc)
      {
        gint first = i + 2, n = 0;

        while (first + n < argc && strchr (argv[first + n], '=') &&
               strncmp (argv[first + n], "--", 2))
          n++;
        bench (in, argv[i + 1], argv + first, n);
        ops = argv;
        i = first + n - 1;
      }
  if (! ops)
    {
      guint  n, k;
      gchar **all = gegl_list_operations (&n);

      for (k = 0; k < n; k++)
        if (g_str_has_prefix (all[k], "forensics:"))
          bench (in, all[k], NULL, 0);
      g_free (all);
    }
  g_object_unref (in);
  gegl_exit ();
  check_cleanup ();
  return 0;
}
