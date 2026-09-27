/*
 * Shared pieces of the check programs (tests/check-*.c)
 *
 * check-common.h
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PASS/FAIL reports, loading the operations from the build (and only
 * them: GEGL_PATH is set to a folder with links to the given modules and
 * GEGL's own operations), synthetic images, and running an operation on
 * a buffer.
 */

#ifndef CHECK_COMMON_H
#define CHECK_COMMON_H

#include <gegl.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WORK "R'G'B'A float"

static gint     n_failed   = 0;
static gint     n_passed   = 0;
static gint     n_messages = 0;
static gchar   *mod_dir    = NULL;
static GSList  *mod_links  = NULL;

static inline void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

static inline void report (const gchar *name, gboolean ok, const gchar *format, ...)
  G_GNUC_PRINTF (3, 4);

static inline void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...)
{
  printf ("%s  %s", ok ? "PASS" : "FAIL", name);
  if (format)
    {
      va_list args;
      gchar  *detail;

      va_start (args, format);
      detail = g_strdup_vprintf (format, args);
      va_end (args);
      printf (": %s", detail);
      g_free (detail);
    }
  printf ("\n");
  fflush (stdout);
  if (ok)
    n_passed++;
  else
    n_failed++;
}

/* check_start (argc, argv, "forensics:error-level", ...): links the
 * modules given as arguments into a folder of their own (GEGL loads every
 * file in a module folder) and starts GEGL with only them and GEGL's own
 * operations. Returns FALSE (after a message) if an operation is missing */
static inline gboolean
check_start (gint    *argc,
             gchar ***argv,
             ...)
{
  va_list      ops;
  const gchar *op;
  gint         i;
  gchar       *path;

  mod_dir = g_dir_make_tmp ("forensics-check-XXXXXX", NULL);
  if (! mod_dir)
    return FALSE;
  for (i = 1; i < *argc; i++)
    {
      gchar *base   = g_path_get_basename ((*argv)[i]);
      gchar *link   = g_build_filename (mod_dir, base, NULL);
      gchar *target = g_canonicalize_filename ((*argv)[i], NULL);

      if (symlink (target, link) != 0)
        {
          fprintf (stderr, "cannot link %s\n", (*argv)[i]);
          return FALSE;
        }
      mod_links = g_slist_prepend (mod_links, link);
      g_free (base);
      g_free (target);
    }
  /* no swap file: the tests stay in memory and leave nothing behind */
  g_setenv ("GEGL_SWAP", "RAM", TRUE);
  path = g_strconcat (mod_dir, G_SEARCHPATH_SEPARATOR_S, GEGL_PLUGINSDIR, NULL);
  g_setenv ("GEGL_PATH", path, TRUE);
  g_free (path);

  gegl_init (NULL, NULL);
  g_log_set_default_handler (log_handler, NULL);

  va_start (ops, argv);
  while ((op = va_arg (ops, const gchar *)))
    if (! gegl_has_operation (op))
      {
        fprintf (stderr, "%s did not load\n", op);
        va_end (ops);
        return FALSE;
      }
  va_end (ops);
  return TRUE;
}

/* after gegl_exit (): removes the links to the modules */
static inline void
check_cleanup (void)
{
  GSList *l;

  /* LeakSanitizer needs the modules to name functions after main () */
  if (! g_getenv ("FORENSICS_CHECK_KEEP_MODULES"))
    {
      for (l = mod_links; l; l = l->next)
        g_unlink (l->data);
      g_rmdir (mod_dir);
    }
  g_slist_free_full (mod_links, g_free);
  g_free (mod_dir);
}

static inline gint
check_end (void)
{
  report ("no_unexpected_warnings_or_criticals", n_messages == 0, "%d",
          n_messages);
  gegl_exit ();
  check_cleanup ();
  printf ("\n%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}

/* random numbers that are the same everywhere */
typedef struct { guint64 s; } Rng;

static inline guint32
rng_next (Rng *r)
{
  r->s = r->s * 6364136223846793005ULL + 1442695040888963407ULL;
  return (guint32) (r->s >> 33);
}

static inline gdouble
rng_uniform (Rng *r)
{
  return rng_next (r) / 2147483648.0;
}

static inline gdouble
rng_gauss (Rng *r)
{
  gdouble u = rng_uniform (r) * 0.999999 + 1e-7, v = rng_uniform (r);

  return sqrt (-2.0 * log (u)) * cos (2.0 * G_PI * v);
}

/* smooth value noise: a lattice of random values, interpolated */
static inline gdouble
value_noise (guint32 seed, gdouble x, gdouble y)
{
  gint    ix = (gint) floor (x), iy = (gint) floor (y);
  gdouble fx = x - ix, fy = y - iy, v[4];
  gint    k;

  for (k = 0; k < 4; k++)
    {
      guint32 h = (guint32) (ix + (k & 1)) * 374761393u +
                  (guint32) (iy + (k >> 1)) * 668265263u + seed * 2246822519u;

      h = (h ^ (h >> 13)) * 1274126177u;
      v[k] = ((h ^ (h >> 16)) & 0xffffff) / 16777216.0;
    }
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  return (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) +
         (v[2] * (1 - fx) + v[3] * fx) * fy;
}

/* a photo-like scene, w x h R'G'B'A float: a sky gradient, hills,
 * textured ground, a few objects with edges, and grain (sigma levels of
 * 8 bit). Alpha 1. */
static inline gfloat *
make_scene (gint w, gint h, guint32 seed, gdouble grain)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  Rng     rng = { seed * 7919u + 1 };
  gint    x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble u = (gdouble) x / MAX (w - 1, 1), v = (gdouble) y / MAX (h - 1, 1);
        gdouble hill = 0.45 + 0.08 * sin (6.0 * u + seed) +
                       0.05 * value_noise (seed, x / 40.0, 0.0);
        gdouble r, g, b, t;
        gfloat *q = p + ((gsize) y * w + x) * 4;

        if (v < hill)
          {
            r = 0.35 + 0.3 * v; g = 0.55 + 0.25 * v; b = 0.9 - 0.1 * v;
            t = value_noise (seed + 1, x / 60.0, y / 25.0);
            r += 0.25 * t; g += 0.25 * t; b += 0.1 * t;     /* clouds */
          }
        else
          {
            t = 0.5 * value_noise (seed + 2, x / 6.0, y / 6.0) +
                0.3 * value_noise (seed + 3, x / 2.0, y / 2.0) +
                0.2 * value_noise (seed + 4, x / 25.0, y / 25.0);
            r = 0.25 + 0.35 * t; g = 0.35 + 0.35 * t; b = 0.1 + 0.2 * t;
          }
        /* a red box and a bright disc: sharp edges */
        if (u > 0.62 && u < 0.8 && v > 0.55 && v < 0.85)
          {
            r = 0.75 + 0.1 * value_noise (seed + 5, x / 3.0, y / 3.0);
            g = 0.15; b = 0.12;
          }
        if (hypot (u - 0.25, v - 0.3) < 0.08)
          r = g = b = 0.95;
        r += grain / 255.0 * rng_gauss (&rng);
        g += grain / 255.0 * rng_gauss (&rng);
        b += grain / 255.0 * rng_gauss (&rng);
        q[0] = CLAMP (r, 0.0, 1.0);
        q[1] = CLAMP (g, 0.0, 1.0);
        q[2] = CLAMP (b, 0.0, 1.0);
        q[3] = 1.0f;
      }
  return p;
}

static inline GeglBuffer *
buffer_from (const gfloat *pixels, gint w, gint h, const gchar *format)
{
  GeglRectangle r   = { 0, 0, w, h };
  GeglBuffer   *buf = gegl_buffer_new (&r, babl_format (format));

  gegl_buffer_set (buf, &r, 0, babl_format (WORK), pixels, GEGL_AUTO_ROWSTRIDE);
  return buf;
}

/* runs op with the properties (name, value, ..., NULL; as gegl_node_set)
 * on the buffer; returns the rect of the output as R'G'B'A float */
static inline gfloat *
run_op_rect (GeglBuffer          *in,
             const GeglRectangle *rect,
             const gchar         *op,
             const gchar         *first,
             va_list              args)
{
  GeglNode *graph = gegl_node_new ();
  GeglNode *src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                                         "buffer", in, NULL);
  GeglNode *node  = gegl_node_new_child (graph, "operation", op, NULL);
  gfloat   *out   = g_new0 (gfloat, (gsize) rect->width * rect->height * 4);

  if (first)
    gegl_node_set_valist (node, first, args);
  gegl_node_link (src, node);
  gegl_node_blit (node, 1.0, rect, babl_format (WORK), out,
                  GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (graph);
  return out;
}

static inline gfloat *
run_op (GeglBuffer  *in,
        const gchar *op,
        const gchar *first,
        ...)
{
  GeglRectangle r = *gegl_buffer_get_extent (in);
  va_list       args;
  gfloat       *out;

  va_start (args, first);
  out = run_op_rect (in, &r, op, first, args);
  va_end (args);
  return out;
}

static inline gfloat *
run_op_on (GeglBuffer          *in,
           const GeglRectangle *rect,
           const gchar         *op,
           const gchar         *first,
           ...)
{
  va_list args;
  gfloat *out;

  va_start (args, first);
  out = run_op_rect (in, rect, op, first, args);
  va_end (args);
  return out;
}

/* the mean of channel c (0 to 2, or -1 for the mean of R, G, B) over a
 * rectangle of a w-wide R'G'B'A float image, optionally leaving out
 * another rectangle */
static inline gdouble
mean_in (const gfloat        *p,
         gint                 w,
         gint                 h,
         const GeglRectangle *r,
         const GeglRectangle *not,
         gint                 c)
{
  gdouble sum = 0;
  gsize   n   = 0;
  gint    x, y;

  for (y = MAX (r->y, 0); y < MIN (r->y + r->height, h); y++)
    for (x = MAX (r->x, 0); x < MIN (r->x + r->width, w); x++)
      {
        const gfloat *q = p + ((gsize) y * w + x) * 4;

        if (not && x >= not->x && x < not->x + not->width &&
            y >= not->y && y < not->y + not->height)
          continue;
        sum += c < 0 ? (q[0] + q[1] + q[2]) / 3.0 : q[c];
        n++;
      }
  return n ? sum / n : 0.0;
}

static inline gdouble
max_abs_diff (const gfloat *a, const gfloat *b, gsize n)
{
  gdouble d = 0;
  gsize   i;

  for (i = 0; i < n; i++)
    {
      gdouble e = fabs ((gdouble) a[i] - b[i]);

      if (! (e <= d))
        d = e;   /* (NaN counts as a difference) */
    }
  return d;
}

static inline gint
cmp_double (gconstpointer a, gconstpointer b)
{
  gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

  return x < y ? -1 : x > y;
}

/* the q-quantile (0 to 1) of n values (sorts them) */
static inline gdouble
quantile (gdouble *v, gsize n, gdouble q)
{
  if (! n)
    return 0;
  qsort (v, n, sizeof *v, cmp_double);
  return v[MIN ((gsize) (q * (n - 1) + 0.5), n - 1)];
}

#endif
