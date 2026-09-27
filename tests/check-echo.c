/*
 * Checks of forensics:echo
 *
 * check-echo.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-echo <echo.so>
 *
 * OpenCV's Laplacian kernels (an impulse shows them: ksize 3 is 2 0 2 /
 * 0 -8 0 / 2 0 2, ksize 5 the sum of 1 0 -2 0 1 along one axis times
 * 1 4 6 4 1 along the other); against a brute force reference (every
 * radius 1 to 4, normalised or by the gain, contrast, grayscale,
 * BORDER_REFLECT_101 at the edges); a blurred region stays dark; the
 * standard checks.
 */

#include "check-common.h"

#define OP "forensics:echo"

static gint
u8 (gfloat v)
{
  if (! (v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 255;
  return (gint) (v * 255.0f + 0.5f);
}

static gint
reflect (gint v, gint n)
{
  if (n <= 1)
    return 0;
  while (v < 0 || v >= n)
    {
      if (v < 0)
        v = -v;
      if (v >= n)
        v = 2 * (n - 1) - v;
    }
  return v;
}

/* binomial coefficients of row n (n + 1 values) */
static void
binomial (gint n, gdouble *k)
{
  gint i, j;

  k[0] = 1;
  for (i = 1; i <= n; i++)
    {
      k[i] = 0;
      for (j = i; j > 0; j--)
        k[j] += k[j - 1];
    }
}

/* the 2D Laplacian kernel of size k x k, as OpenCV documents it */
static void
laplacian_kernel (gint ksize, gdouble *ker)
{
  gint i, j;

  if (ksize == 3)
    {
      static const gdouble k3[9] = { 2, 0, 2, 0, -8, 0, 2, 0, 2 };

      memcpy (ker, k3, sizeof k3);
      return;
    }
  {
    gdouble b2[32], d[32], s[32];

    binomial (ksize - 3, b2);            /* ksize - 2 values */
    for (i = 0; i < ksize; i++)
      d[i] = 0;
    for (i = 0; i < ksize - 2; i++)
      {
        d[i] += b2[i];
        d[i + 1] -= 2 * b2[i];
        d[i + 2] += b2[i];
      }
    binomial (ksize - 1, s);
    for (j = 0; j < ksize; j++)
      for (i = 0; i < ksize; i++)
        ker[j * ksize + i] = d[i] * s[j] + s[i] * d[j];
  }
}

static gfloat *
reference (const gfloat *img, gint w, gint h, gint radius, gdouble contrast, gboolean norm,
           gdouble gain, gboolean gray)
{
  gint     k = 2 * radius + 1, x, y, c, i, j, high;
  gdouble *ker = g_new (gdouble, (gsize) k * k), *lap = g_new (gdouble, (gsize) w * h * 3);
  gdouble  lo[3], hi[3], scale;
  gfloat  *out = g_new (gfloat, (gsize) w * h * 4);

  laplacian_kernel (k, ker);
  for (c = 0; c < 3; c++)
    {
      lo[c] = G_MAXDOUBLE;
      hi[c] = -G_MAXDOUBLE;
    }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      for (c = 0; c < 3; c++)
        {
          gdouble s = 0;

          for (j = 0; j < k; j++)
            for (i = 0; i < k; i++)
              s += ker[j * k + i] *
                   u8 (img[((gsize) reflect (y + j - radius, h) * w + reflect (x + i - radius, w)) * 4 + c]);
          lap[((gsize) y * w + x) * 3 + c] = fabs (s);
          lo[c] = MIN (lo[c], fabs (s));
          hi[c] = MAX (hi[c], fabs (s));
        }
  high = (gint) (contrast / 100.0 * 255.0);
  scale = high >= 255 ? -1 : 255.0 / (255 - high);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gint v8[3];

        for (c = 0; c < 3; c++)
          {
            gdouble v = lap[((gsize) y * w + x) * 3 + c], n;

            n = norm ? (hi[c] > lo[c] ? floor ((v - lo[c]) * 255 / (hi[c] - lo[c]) + 0.5) : 0) : v * gain;
            v8[c] = scale < 0 ? 255 : (gint) CLAMP (floor (MIN (n, 255.0) * scale), 0, 255);
          }
        for (c = 0; c < 3; c++)
          out[((gsize) y * w + x) * 4 + c] = gray ? ((v8[0] * 4899 + v8[1] * 9617 + v8[2] * 1868 + 8192) >> 14) / 255.0f
                                                  : v8[c] / 255.0f;
        out[((gsize) y * w + x) * 4 + 3] = img[((gsize) y * w + x) * 4 + 3];
      }
  g_free (ker);
  g_free (lap);
  return out;
}

static void
test_kernels (void)
{
  const gint  w = 21, h = 21;
  gfloat     *img = g_new0 (gfloat, (gsize) w * h * 4);
  GeglBuffer *buf;
  gint        radius, bad = 0, x, y;
  gsize       i;

  for (i = 0; i < (gsize) w * h; i++)
    img[4 * i + 3] = 1;
  img[(10 * w + 10) * 4] = 1.0f;     /* a red impulse of 255 */
  buf = buffer_from (img, w, h, WORK);
  for (radius = 1; radius <= 3; radius++)
    {
      gint     k = 2 * radius + 1;
      gdouble *ker = g_new (gdouble, (gsize) k * k), top = 0;
      gfloat  *out = run_op (buf, OP, "radius", radius, "normalize", FALSE, "gain", 1.0 / 255,
                             "contrast", 0.0, NULL);

      laplacian_kernel (k, ker);
      for (i = 0; i < (gsize) k * k; i++)
        top = MAX (top, fabs (ker[i]));
      /* the response to an impulse is the kernel, mirrored (it is symmetric) */
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            gint    dx = x - 10, dy = y - 10;
            gdouble want = abs (dx) <= radius && abs (dy) <= radius
                           ? fabs (ker[(dy + radius) * k + dx + radius]) : 0;
            gdouble got = out[((gsize) y * w + x) * 4] * 255;

            /* (the 8 bit result of |L| x gain: truncated) */
            if (fabs (got - floor (want)) > 1e-3)
              bad++;
          }
      g_free (out);
      g_free (ker);
    }
  report ("impulse_shows_opencv_kernels", bad == 0,
          "ksize 3 (2 0 2 / 0 -8 0 / 2 0 2), 5 and 7: %d pixels differ", bad);
  g_object_unref (buf);
  g_free (img);
}

static void
test_reference (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 2, 2 }, { 5, 4 }, { 23, 17 }, { 64, 48 } };
  gint    s, radius, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1];
      gfloat     *img = make_scene (w, h, 90 + s, 4.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (radius = 1; radius <= 4; radius++)
        {
          gint v;

          for (v = 0; v < 3; v++)
            {
              gboolean norm = v != 1, gray = v == 2;
              gdouble  contrast = v == 0 ? 85.0 : 40.0;
              gfloat  *got = run_op (buf, OP, "radius", radius, "normalize", norm, "gain", 0.3,
                                     "contrast", contrast, "grayscale", gray, NULL);
              gfloat  *want = reference (img, w, h, radius, contrast, norm, 0.3, gray);
              gdouble  d = max_abs_diff (got, want, (gsize) w * h * 4);

              n++;
              worst = MAX (worst, d);
              if (d > 1e-6)
                {
                  bad++;
                  printf ("      %dx%d radius %d variant %d: %g\n", w, h, radius, v, d);
                }
              g_free (got);
              g_free (want);
            }
        }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0, "%d cases (1x1 to 64x48, radius 1 to 4, normalised "
          "or by the gain, contrast 85 and 40, gray): largest difference %.2g", n, worst);
}

static void
test_blurred_region (void)
{
  const gint    w = 384, h = 256;
  GeglRectangle r = { 128, 64, 128, 128 };
  gfloat       *img = make_texture (w, h, 3.0), *blur = g_memdup2 (img, (gsize) w * h * 16);
  GeglBuffer   *buf;
  gfloat       *out;
  GeglRectangle inner = { r.x + 8, r.y + 8, r.width - 16, r.height - 16 };
  GeglRectangle all = { 0, 0, w, h }, around = { r.x - 8, r.y - 8, r.width + 16, r.height + 16 };
  gdouble       in, rest;
  gint          x, y, c, i, j;

  /* a 5 x 5 box blur: out of focus */
  for (y = r.y; y < r.y + r.height; y++)
    for (x = r.x; x < r.x + r.width; x++)
      for (c = 0; c < 3; c++)
        {
          gdouble s = 0;

          for (j = -2; j <= 2; j++)
            for (i = -2; i <= 2; i++)
              s += img[((gsize) (y + j) * w + x + i) * 4 + c];
          blur[((gsize) y * w + x) * 4 + c] = (gfloat) (s / 25);
        }
  buf = buffer_from (blur, w, h, WORK);
  out = run_op (buf, OP, NULL);
  in = mean_in (out, w, h, &inner, NULL, -1);
  rest = mean_in (out, w, h, &all, &around, -1);
  report ("blurred_region_darker", in < 0.7 * rest,
          "defaults (radius 2, contrast 85, normalised): blurred region %.3f, the rest %.3f "
          "(x %.2f)", in, rest, in / rest);
  g_free (out);
  g_object_unref (buf);
  g_free (blur);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <echo.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_kernels ();
  test_reference ();
  test_blurred_region ();
  check_standard ("normalized", OP, NULL, 0);
  check_standard ("by_gain", OP, "normalize", FALSE);
  check_standard ("radius_4", OP, "radius", 4);
  return check_end ();
}
