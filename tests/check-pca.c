/*
 * Checks of forensics:pca
 *
 * check-pca.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-pca <pca.so>
 *
 * Colors spread along three known orthogonal directions with falling
 * variance: each component's projection follows its coordinate; colors
 * on a line are at distance 0 from it; a region whose colors lie off the
 * image's color plane stands out along the third component; 1 x 1
 * images; NaN, alpha, invert, the scale, determinism.
 */

#include "check-common.h"

#define OP "forensics:pca"

static const gdouble A[3] = { 0.6, 0.7, 0.3873 };   /* about unit length */

/* three orthonormal directions: a, then b and c orthogonal to it */
static void
axes (gdouble a[3], gdouble b[3], gdouble c[3])
{
  gdouble n = sqrt (A[0] * A[0] + A[1] * A[1] + A[2] * A[2]);
  gint    i;

  for (i = 0; i < 3; i++)
    a[i] = A[i] / n;
  /* b: a x (0, 0, 1), normalised; c = a x b */
  b[0] = a[1]; b[1] = -a[0]; b[2] = 0;
  n = sqrt (b[0] * b[0] + b[1] * b[1]);
  b[0] /= n; b[1] /= n;
  c[0] = a[1] * b[2] - a[2] * b[1];
  c[1] = a[2] * b[0] - a[0] * b[2];
  c[2] = a[0] * b[1] - a[1] * b[0];
}

static gdouble
correlation (const gdouble *x, const gfloat *y, gsize n)
{
  gdouble mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0;
  gsize   i;

  for (i = 0; i < n; i++)
    {
      mx += x[i] / n;
      my += y[4 * i] / (gdouble) n;
    }
  for (i = 0; i < n; i++)
    {
      sxy += (x[i] - mx) * (y[4 * i] - my);
      sxx += (x[i] - mx) * (x[i] - mx);
      syy += (y[4 * i] - my) * (y[4 * i] - my);
    }
  return sxy / sqrt (sxx * syy);
}

static void
test_components (void)
{
  const gint  w = 200, h = 150;
  gsize       n = (gsize) w * h, i;
  gfloat     *img = g_new (gfloat, n * 4);
  gdouble    *s[3], a[3], b[3], c[3], r[3];
  gdouble     sd[3] = { 0.12, 0.04, 0.01 };
  Rng         rng = { 21 };
  GeglBuffer *buf;
  gint        k;

  axes (a, b, c);
  for (k = 0; k < 3; k++)
    s[k] = g_new (gdouble, n);
  for (i = 0; i < n; i++)
    {
      gint ch;

      for (k = 0; k < 3; k++)
        s[k][i] = sd[k] * rng_gauss (&rng);
      for (ch = 0; ch < 3; ch++)
        img[4 * i + ch] = 0.5 + s[0][i] * a[ch] + s[1][i] * b[ch] + s[2][i] * c[ch];
      img[4 * i + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  for (k = 0; k < 3; k++)
    {
      gfloat *e = run_op (buf, OP, "component", k + 1, NULL);

      r[k] = fabs (correlation (s[k], e, n));
      g_free (e);
    }
  /* (the sign of a component is its own choice: |r|) */
  report ("components_found", r[0] > 0.999 && r[1] > 0.99 && r[2] > 0.99,
          "standard deviations 0.12, 0.04, 0.01 along three directions: "
          "correlations %.4f, %.4f, %.4f", r[0], r[1], r[2]);

  /* projection at scale 1: 2 standard deviations to 0 and 1 */
  {
    gfloat *e = run_op (buf, OP, "component", 2, NULL);
    gdouble m = 0, v = 0;

    for (i = 0; i < n; i++)
      m += e[4 * i] / (gdouble) n;
    for (i = 0; i < n; i++)
      v += (e[4 * i] - m) * (e[4 * i] - m) / n;
    report ("projection_scale", fabs (m - 0.5) < 1e-3 && fabs (sqrt (v) - 0.25) < 0.01,
            "mean %.4f, standard deviation %.4f (0.5 and 0.25)", m, sqrt (v));
    g_free (e);
  }
  g_object_unref (buf);

  /* on a line: distance 0 from the first component */
  for (i = 0; i < n; i++)
    {
      gint ch;

      for (ch = 0; ch < 3; ch++)
        img[4 * i + ch] = 0.5 + s[0][i] * a[ch];
    }
  buf = buffer_from (img, w, h, WORK);
  {
    gfloat *e = run_op (buf, OP, "component", 1, "mode", 1, NULL);
    gdouble mx = 0;

    for (i = 0; i < n; i++)
      mx = MAX (mx, e[4 * i]);
    report ("colors_on_a_line_distance_0", mx < 1e-3,
            "largest distance %.2g", mx);
    g_free (e);
  }
  g_object_unref (buf);
  for (k = 0; k < 3; k++)
    g_free (s[k]);
  g_free (img);
}

static void
test_region (void)
{
  const gint    w = 400, h = 300;
  gsize         n = (gsize) w * h, i;
  GeglRectangle r = { 140, 100, 120, 100 };
  gfloat       *img = g_new (gfloat, n * 4), *e;
  gdouble       a[3], b[3], c[3], in = 0, out = 0;
  gsize         n_in = 0, n_out = 0;
  Rng           rng = { 22 };
  GeglBuffer   *buf;

  /* an image whose colors vary along two directions (brightness and one
   * hue axis, as most photos), with a little noise along the third; the
   * region's colors moved 0.03 along the third direction: they relate
   * differently, while their brightness and hue stay in the image's
   * range */
  axes (a, b, c);
  for (i = 0; i < n; i++)
    {
      gint    x = i % w, y = i / w, ch;
      gdouble s1 = 0.12 * rng_gauss (&rng), s2 = 0.05 * rng_gauss (&rng);
      gdouble s3 = 0.004 * rng_gauss (&rng);
      gboolean inside = x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;

      if (inside)
        s3 += 0.03;
      for (ch = 0; ch < 3; ch++)
        img[4 * i + ch] = 0.5 + s1 * a[ch] + s2 * b[ch] + s3 * c[ch];
      img[4 * i + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  e = run_op (buf, OP, "component", 3, NULL);
  for (i = 0; i < n; i++)
    {
      gint    x = i % w, y = i / w;
      gdouble v = fabs (e[4 * i] - 0.5);

      if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
        {
          in += v;
          n_in++;
        }
      else
        {
          out += v;
          n_out++;
        }
    }
  in /= n_in;
  out /= n_out;
  /* (the region shifts the mean a little: "elsewhere" is off 0.5 too) */
  report ("region_off_the_color_plane_on_the_third_component", in > 3 * out,
          "|value - 0.5| on component 3: %.3f in the region, %.3f elsewhere "
          "(x %.1f)", in, out, in / out);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_edges (void)
{
  gfloat      one[4] = { 0.3f, 0.6f, 0.2f, 0.7f };
  GeglBuffer *b1 = buffer_from (one, 1, 1, WORK);
  gfloat     *e1 = run_op (b1, OP, NULL);
  const gint  w = 60, h = 40;
  gfloat     *img = make_scene (w, h, 80, 3.0), *clean;
  GeglBuffer *b2, *b3;
  gfloat     *e2, *e3, *e4, *e5;
  gsize       i;
  gboolean    ok = TRUE;
  gint        threads;
  const gfloat bad[] = { NAN, INFINITY, -INFINITY };

  report ("one_pixel", e1[0] == 0.5f && e1[3] == 0.7f, "gray 0.5, alpha kept");
  clean = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h * 4; i += 13)
    if (i % 4 != 3)
      {
        gfloat v = bad[(i / 13) % 3];

        img[i] = v;
        clean[i] = v == v && v > 0 ? 1.0f : 0.0f;
      }
  for (i = 0; i < (gsize) w * h; i++)
    img[4 * i + 3] = clean[4 * i + 3] = (i % 5) / 4.0f;
  b2 = buffer_from (img, w, h, WORK);
  b3 = buffer_from (clean, w, h, WORK);
  e2 = run_op (b2, OP, "mode", 1, NULL);
  e3 = run_op (b3, OP, "mode", 1, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (! isfinite (e2[i]))
      ok = FALSE;
  for (i = 0; i < (gsize) w * h; i++)
    if (e2[4 * i + 3] != img[4 * i + 3])
      ok = FALSE;
  report ("nan_and_alpha", ok && max_abs_diff (e2, e3, (gsize) w * h * 4) == 0.0,
          "finite, alpha kept, as with NaN as 0 and infinities clamped");
  e4 = run_op (b3, OP, "mode", 1, "invert", TRUE, NULL);
  ok = TRUE;
  for (i = 0; i < (gsize) w * h; i++)
    if (fabsf (e4[4 * i] - (1 - e3[4 * i])) > 1e-6f)
      ok = FALSE;
  report ("invert", ok, NULL);
  g_object_get (gegl_config (), "threads", &threads, NULL);
  g_object_set (gegl_config (), "threads", 1, NULL);
  e5 = run_op (b3, OP, "mode", 1, NULL);
  g_object_set (gegl_config (), "threads", threads, NULL);
  report ("deterministic", max_abs_diff (e3, e5, (gsize) w * h * 4) == 0.0, NULL);
  g_free (e1); g_free (e2); g_free (e3); g_free (e4); g_free (e5);
  g_object_unref (b1); g_object_unref (b2); g_object_unref (b3);
  g_free (img); g_free (clean);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <pca.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_components ();
  test_region ();
  test_edges ();

  return check_end ();
}
