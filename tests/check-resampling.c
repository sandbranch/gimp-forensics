/*
 * Checks of forensics:resampling
 *
 * check-resampling.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-resampling <resampling.so>
 *
 * The probability map against Kirchner's formula; the score against a
 * brute force DFT; a region enlarged by 1.2 (bilinear) or 1.5 (bicubic)
 * scores above every block elsewhere, by 1.2 or 1.1 (bicubic) with most
 * of its blocks; one shrunk to 0.8 does not stand out; after a JPEG save at 95 the region no longer
 * stands out (the known weakness, reported and checked); the spectrum
 * view; the standard checks.
 */

#include "check-jpeg.h"

#define OP "forensics:resampling"

static gint
u8 (gfloat v)
{
  if (! (v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 255;
  return (gint) (v * 255.0f + 0.5f);
}

static gdouble
luma (const gfloat *p)
{
  return (u8 (p[0]) * 4899 + u8 (p[1]) * 9617 + u8 (p[2]) * 1868 + 8192) >> 14;
}

static gdouble
cubic (gdouble t)
{
  /* Keys, a = -0.5 */
  t = fabs (t);
  if (t < 1)
    return 1.5 * t * t * t - 2.5 * t * t + 1;
  if (t < 2)
    return -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2;
  return 0;
}

/* the region r of dst replaced by the part of src from its corner,
 * enlarged by f (bicubic or bilinear), rounded to 8 bit */
static void
resample_into (gfloat *dst, const gfloat *src, gint w, gint h, const GeglRectangle *r,
               gdouble f, gboolean bicubic)
{
  gint x, y, c, i, j;

  for (y = 0; y < r->height; y++)
    for (x = 0; x < r->width; x++)
      for (c = 0; c < 3; c++)
        {
          gdouble sx = (x + 0.5) / f - 0.5 + r->x, sy = (y + 0.5) / f - 0.5 + r->y;
          gint    ix = (gint) floor (sx), iy = (gint) floor (sy);
          gdouble s = 0, ws = 0;

          for (j = bicubic ? -1 : 0; j <= (bicubic ? 2 : 1); j++)
            for (i = bicubic ? -1 : 0; i <= (bicubic ? 2 : 1); i++)
              {
                gdouble wx = bicubic ? cubic (sx - (ix + i)) : 1 - fabs (sx - (ix + i));
                gdouble wy = bicubic ? cubic (sy - (iy + j)) : 1 - fabs (sy - (iy + j));
                gint    px = CLAMP (ix + i, 0, w - 1), py = CLAMP (iy + j, 0, h - 1);

                s += wx * wy * src[((gsize) py * w + px) * 4 + c];
                ws += wx * wy;
              }
          dst[((gsize) (r->y + y) * w + r->x + x) * 4 + c] =
            CLAMP (roundf ((gfloat) (s / ws) * 255) / 255, 0, 1);
        }
}

static void
test_pmap_and_score (void)
{
  const gint  w = 70, h = 50;
  gfloat     *img = make_texture (w, h, 3.0), *pm, *sc;
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gint        x, y, bad = 0, u, v, b = 32;
  gdouble     worst = 0, re, im, peak = 0, med, score;
  gdouble    *mag = g_new (gdouble, b * b), *vals = g_new (gdouble, b * b), *p = g_new (gdouble, b * b);
  gint        n = 0;

  pm = run_op (buf, OP, "mode", 2, NULL);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble c = luma (img + ((gsize) y * w + x) * 4), s = 0;
        gint    i, j;

        for (j = -1; j <= 1; j++)
          for (i = -1; i <= 1; i++)
            {
              gdouble k = (i == 0 && j == 0) ? 0 : (i == 0 || j == 0) ? 0.5 : -0.25;

              s += k * luma (img + ((gsize) CLAMP (y + j, 0, h - 1) * w + CLAMP (x + i, 0, w - 1)) * 4);
            }
        worst = MAX (worst, fabs (pm[((gsize) y * w + x) * 4] - exp (-(c - s) * (c - s))));
      }
  report ("probability_map", worst < 1e-6, "p = exp (-e^2) of Kirchner's predictor: largest "
          "difference %.2g", worst);

  /* the score of the first 32 x 32 block by a brute force DFT */
  sc = run_op (buf, OP, "block-size", 32, "scale", 1.0, NULL);
  {
    gdouble mean = 0;

    for (y = 0; y < b; y++)
      for (x = 0; x < b; x++)
        mean += pm[((gsize) y * w + x) * 4];
    mean /= b * b;
    for (y = 0; y < b; y++)
      for (x = 0; x < b; x++)
        {
          gdouble wx = 0.5 - 0.5 * cos (2 * G_PI * x / (b - 1));
          gdouble wy = 0.5 - 0.5 * cos (2 * G_PI * y / (b - 1));

          p[y * b + x] = (pm[((gsize) y * w + x) * 4] - mean) * wx * wy;
        }
  }
  for (v = 0; v < b; v++)
    for (u = 0; u < b; u++)
      {
        gdouble fu = (u <= b / 2 ? u : u - b) / (gdouble) b, fv = (v <= b / 2 ? v : v - b) / (gdouble) b;
        gdouble du = fabs (fu * 8 - floor (fu * 8 + 0.5)) / 8 * b;
        gdouble dv = fabs (fv * 8 - floor (fv * 8 + 0.5)) / 8 * b;

        re = im = 0;
        for (y = 0; y < b; y++)
          for (x = 0; x < b; x++)
            {
              gdouble a = -2 * G_PI * ((gdouble) u * x + (gdouble) v * y) / b;

              re += p[y * b + x] * cos (a);
              im += p[y * b + x] * sin (a);
            }
        mag[v * b + u] = hypot (re, im);
        if (hypot (fu, fv) > 0.1 && ! (du < 1.5 && dv < 1.5))
          {
            vals[n++] = mag[v * b + u];
            peak = MAX (peak, mag[v * b + u]);
          }
      }
  qsort (vals, n, sizeof *vals, cmp_double);
  med = vals[n / 2];
  score = peak / med;
  for (y = 0; y < b; y++)
    for (x = 0; x < b; x++)
      if (fabs (sc[((gsize) y * w + x) * 4] - score) > 1e-4 * score)
        bad++;
  report ("score_same_as_brute_force_dft", bad == 0, "32 x 32 block: score %.4f, the operation "
          "%.4f", score, sc[0]);
  g_free (mag);
  g_free (vals);
  g_free (p);
  g_free (pm);
  g_free (sc);
  g_object_unref (buf);
  g_free (img);
}

/* the scores of the 64 x 64 blocks inside r, and the highest elsewhere */
static void
scores (const gfloat *s, gint w, gint h, const GeglRectangle *r, gdouble *min_in,
        gdouble *median_in, gdouble *max_out)
{
  gdouble in[64];
  gint    bx, by, n = 0;

  *max_out = 0;
  for (by = 0; by + 64 <= h; by += 64)
    for (bx = 0; bx + 64 <= w; bx += 64)
      {
        gdouble v = s[((gsize) (by + 32) * w + bx + 32) * 4] * 16;
        gboolean inside = bx >= r->x && bx + 64 <= r->x + r->width && by >= r->y &&
                          by + 64 <= r->y + r->height;
        gboolean away = bx + 64 <= r->x || bx >= r->x + r->width || by + 64 <= r->y ||
                        by >= r->y + r->height;

        if (inside)
          in[n++] = v;
        else if (away)
          *max_out = MAX (*max_out, v);
      }
  *min_in = quantile (in, n, 0.0);
  *median_in = quantile (in, n, 0.5);
}

static void
test_resampled_regions (void)
{
  const gint    w = 512, h = 512;
  GeglRectangle r = { 128, 128, 256, 256 };
  /* found: 2 every block of the region above every block elsewhere, 1
   * most of them (the median), 0 not found */
  static const struct { gdouble f; gboolean bicubic; gint found, jpeg; } cases[] = {
    { 1.2, TRUE, 1, 0 }, { 1.2, FALSE, 2, 0 }, { 1.5, TRUE, 2, 0 },
    { 1.1, TRUE, 1, 0 }, { 0.8, TRUE, 0, 0 }, { 1.2, TRUE, 0, 95 },
  };
  gint k;

  for (k = 0; k < (gint) G_N_ELEMENTS (cases); k++)
    {
      gfloat     *img = make_texture (w, h, 3.0), *src = make_texture (w, h, 3.0), *s;
      GeglBuffer *buf;
      gdouble     lo, med, hi;
      gchar      *name;

      resample_into (img, src, w, h, &r, cases[k].f, cases[k].bicubic);
      if (cases[k].jpeg)
        {
          guint8 *q = to_u8 (img, w, h), *j = jpeg_file (q, w, h, cases[k].jpeg, FX_CHROMA_420);

          g_free (img);
          img = from_u8 (j, w, h);
          g_free (q);
          g_free (j);
        }
      buf = buffer_from (img, w, h, WORK);
      s = run_op (buf, OP, NULL);
      scores (s, w, h, &r, &lo, &med, &hi);
      name = g_strdup_printf ("%s_%.1f_%s%s", cases[k].found == 2 ? "found" :
                              cases[k].found ? "mostly_found" : "not_found",
                              cases[k].f, cases[k].bicubic ? "bicubic" : "bilinear",
                              cases[k].jpeg ? "_after_jpeg_95" : "");
      for (gchar *c = name; *c; c++)
        if (*c == '.')
          *c = '_';
      report (name, cases[k].found == 2 ? lo > hi : cases[k].found ? med > hi : med < hi,
              "region's 16 blocks: lowest %.1f, median %.1f; highest elsewhere %.1f%s", lo, med,
              hi, cases[k].found == 2 ? "" : cases[k].found ? " (some blocks as low as elsewhere)"
              : " (the known limit)");
      g_free (name);
      g_free (s);
      g_object_unref (buf);
      g_free (src);
      g_free (img);
    }
}

static void
test_spectrum (void)
{
  const gint    w = 256, h = 128;
  GeglRectangle r = { 128, 0, 128, 128 };
  gfloat       *img = make_texture (w, h, 3.0), *src = make_texture (w, h, 3.0), *s;
  GeglBuffer   *buf;
  gdouble       top_in = 0, top_out = 0;
  gint          x, y;

  resample_into (img, src, w, h, &r, 1.2, FALSE);
  buf = buffer_from (img, w, h, WORK);
  s = run_op (buf, OP, "mode", 1, "block-size", 128, NULL);
  /* the spectrum is scaled to its peak (1): the resampled block has a
   * peak far above its median, the other none */
  {
    gdouble a[128 * 128], b[128 * 128];
    gint    n = 0;

    for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
        {
          a[n] = s[((gsize) y * w + x) * 4];
          b[n++] = s[((gsize) y * w + x + 128) * 4];
          top_out = MAX (top_out, a[n - 1]);
          top_in = MAX (top_in, b[n - 1]);
        }
    report ("spectrum_view", top_in == 1.0 && top_out == 1.0 &&
            quantile (b, n, 0.5) < quantile (a, n, 0.5) &&
            s[((gsize) 64 * w + 64) * 4] == 0.0f,
            "log magnitude over its peak: medians %.3f (resampled) and %.3f; the centre "
            "black", quantile (b, n, 0.5), quantile (a, n, 0.5));
  }
  g_free (s);
  g_object_unref (buf);
  g_free (src);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <resampling.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_pmap_and_score ();
  test_resampled_regions ();
  test_spectrum ();
  check_standard ("score_blocks_32", OP, "block-size", 32);
  check_standard ("spectrum", OP, "mode", 1);
  check_standard ("pmap", OP, "mode", 2);
  return check_end ();
}
