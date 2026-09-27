/*
 * Checks of forensics:wavelet-noise
 *
 * check-wavelet-noise.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-wavelet-noise <wavelet-noise.so>
 *
 * The Daubechies 8 filter's properties (checked on the numbers the
 * operation uses, copied here: sum sqrt 2, orthonormal under shifts by 2,
 * 8 vanishing moments of the high pass); against a brute force reference
 * (the HH subband with pywt's symmetric extension, block medians); the
 * noise level of white noise of known sigma; a region with more noise; a
 * smooth ramp gives only the rounding's noise; normalizing; the standard
 * checks.
 */

#include "check-common.h"

#define OP "forensics:wavelet-noise"

static const gdouble LO[16] = {
  -0.00011747678400228192, 0.0006754494059985568, -0.0003917403729959771,
  -0.00487035299301066, 0.008746094047015655, 0.013981027917015516,
  -0.04408825393106472, -0.01736930100202211, 0.128747426620186,
  0.00047248457399797254, -0.2840155429624281, -0.015829105256023893,
  0.5853546836548691, 0.6756307362980128, 0.3128715909144659,
  0.05441584224308161
};

static gint
u8 (gfloat v)
{
  if (! (v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 255;
  return (gint) (v * 255.0f + 0.5f);
}

static void
test_filter (void)
{
  gdouble sum = 0, worst = 0, hi[16];
  gint    k, m, p;

  for (k = 0; k < 16; k++)
    {
      sum += LO[k];
      hi[k] = (k % 2 ? -1.0 : 1.0) * LO[15 - k];
    }
  for (m = 0; m < 8; m++)
    {
      gdouble s = 0;

      for (k = 0; k + 2 * m < 16; k++)
        s += LO[k] * LO[k + 2 * m];
      worst = MAX (worst, fabs (s - (m == 0 ? 1.0 : 0.0)));
    }
  for (p = 0; p < 8; p++)
    {
      gdouble s = 0;

      for (k = 0; k < 16; k++)
        s += hi[k] * pow (k, p);
      /* (the moments grow with k^p: relative to the size of the terms) */
      worst = MAX (worst, fabs (s) / pow (15, p));
    }
  report ("db8_filter", fabs (sum - G_SQRT2) < 1e-12 && worst < 1e-9,
          "sum %.15f (sqrt 2), orthonormality and vanishing moments within %.1e", sum, worst);
}

static gint
symmetric (gint v, gint n)
{
  if (n <= 1)
    return 0;
  for (;;)
    {
      if (v < 0)
        v = -v - 1;
      else if (v >= n)
        v = 2 * n - 1 - v;
      else
        return v;
    }
}

static int
cmp_f (const void *a, const void *b)
{
  gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

  return x < y ? -1 : x > y;
}

/* the noise level per block, as the operation should compute it */
static gdouble *
reference_sigma (const gfloat *img, gint w, gint h, gint b, gint *nbx, gint *nby)
{
  gint     cw = (w + 1) / 2, ch = (h + 1) / 2, x, y, k, j;
  gdouble *g = g_new (gdouble, (gsize) w * h), *hh = g_new (gdouble, (gsize) cw * ch);
  gdouble *sig, hi[16];

  for (k = 0; k < 16; k++)
    hi[k] = (k % 2 ? -1.0 : 1.0) * LO[15 - k];
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        const gfloat *p = img + ((gsize) y * w + x) * 4;

        g[(gsize) y * w + x] = (u8 (p[0]) * 4899 + u8 (p[1]) * 9617 + u8 (p[2]) * 1868 + 8192) >> 14;
      }
  for (y = 0; y < ch; y++)
    for (x = 0; x < cw; x++)
      {
        gdouble s = 0;

        for (j = 0; j < 16; j++)
          for (k = 0; k < 16; k++)
            s += hi[j] * hi[k] * g[(gsize) symmetric (2 * y - 7 + j, h) * w + symmetric (2 * x - 7 + k, w)];
        hh[(gsize) y * cw + x] = fabs (s);
      }
  *nbx = (cw + b - 1) / b;
  *nby = (ch + b - 1) / b;
  sig = g_new (gdouble, (gsize) *nbx * *nby);
  for (y = 0; y < *nby; y++)
    for (x = 0; x < *nbx; x++)
      {
        gdouble v[64 * 64];
        gint    n = 0, i;

        for (j = y * b; j < MIN (y * b + b, ch); j++)
          for (i = x * b; i < MIN (x * b + b, cw); i++)
            v[n++] = hh[(gsize) j * cw + i];
        qsort (v, n, sizeof *v, cmp_f);
        sig[(gsize) y * *nbx + x] = (n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2])) / 0.6745;
      }
  g_free (g);
  g_free (hh);
  return sig;
}

static void
test_reference (void)
{
  static const gint sizes[][3] = { { 1, 1, 2 }, { 5, 3, 2 }, { 17, 16, 4 }, { 40, 29, 8 },
                                   { 101, 67, 8 }, { 70, 70, 5 } };
  gint    s, bad = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1], b = sizes[s][2], nbx, nby, x, y;
      gfloat     *img = make_scene (w, h, 80 + s, 5.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);
      gfloat     *got = run_op (buf, OP, "block-size", b, "gain", 1.0, NULL);
      gdouble    *sig = reference_sigma (img, w, h, b, &nbx, &nby);
      gdouble     d = 0;

      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            gdouble want = sig[(gsize) (y / (2 * b)) * nbx + x / (2 * b)] / 255.0;

            d = MAX (d, fabs (got[((gsize) y * w + x) * 4] - want));
          }
      worst = MAX (worst, d);
      if (d > 1e-5)
        {
          bad++;
          printf ("      %dx%d block %d: %g\n", w, h, b, d);
        }
      g_free (sig);
      g_free (got);
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0, "%d sizes (1x1 to 101x67, blocks of 2 to 8): largest "
          "difference %.2g", (gint) G_N_ELEMENTS (sizes), worst);
}

/* the mean noise level shown inside and outside r (in 8 bit levels) */
static void
levels (const gfloat *e, gint w, gint h, const GeglRectangle *r, gdouble gain, gdouble *in,
        gdouble *out)
{
  GeglRectangle all = { 0, 0, w, h };

  *in = mean_in (e, w, h, r, NULL, 0) * 255 / gain;
  *out = mean_in (e, w, h, &all, r, 0) * 255 / gain;
}

static void
test_known_noise (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 192, 128, 128, 128 };
  gfloat       *img = g_new (gfloat, (gsize) w * h * 4);
  Rng           rng = { 21 };
  GeglBuffer   *buf;
  gfloat       *e;
  gdouble       in, out, inside_sigma = 8.0, sigma = 3.0;
  gint          x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gboolean inside = x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        gfloat   v = 0.5f + (gfloat) ((inside ? inside_sigma : sigma) / 255.0 * rng_gauss (&rng));
        gfloat  *p = img + ((gsize) y * w + x) * 4;

        p[0] = p[1] = p[2] = v;
        p[3] = 1;
      }
  buf = buffer_from (img, w, h, WORK);
  e = run_op (buf, OP, "gain", 1.0, NULL);
  levels (e, w, h, &r, 1.0, &in, &out);
  {
    /* inside: the blocks of the region away from its edge, where the 16
     * taps of the filter reach the other noise */
    GeglRectangle inner = { r.x + 16, r.y + 16, r.width - 32, r.height - 32 };

    in = mean_in (e, w, h, &inner, NULL, 0) * 255;
  }
  /* white noise of sigma s, rounded to 8 bit: sqrt (s^2 + 1/12) */
  report ("white_noise_level", fabs (out / sqrt (sigma * sigma + 1 / 12.0) - 1) < 0.04 &&
          fabs (in / sqrt (inside_sigma * inside_sigma + 1 / 12.0) - 1) < 0.04,
          "sigma %.1f estimated %.3f, sigma %.1f in the region estimated %.3f (its blocks "
          "16 pixels and more from its edge)", sigma, out, inside_sigma, in);
  g_free (e);
  g_object_unref (buf);

  /* a smooth quadratic ramp: the vanishing moments leave only rounding */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = img + ((gsize) y * w + x) * 4;
        gdouble u = x / (gdouble) w, v = y / (gdouble) h;

        p[0] = p[1] = p[2] = (gfloat) (0.1 + 0.5 * u * u + 0.3 * v - 0.2 * u * v);
        p[3] = 1;
      }
  buf = buffer_from (img, w, h, WORK);
  e = run_op (buf, OP, "gain", 1.0, NULL);
  {
    GeglRectangle inner = { 32, 32, w - 64, h - 64 };

    in = mean_in (e, w, h, &inner, NULL, 0) * 255;
  }
  report ("smooth_ramp_only_rounding", in < 0.5, "estimated %.3f levels (the 8 bit rounding "
          "alone, sqrt (1/12) = 0.29, if it were noise)", in);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_normalize (void)
{
  const gint  w = 300, h = 200;
  gfloat     *img = make_scene (w, h, 3, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e = run_op (buf, OP, "normalize", TRUE, NULL);
  gfloat      lo = 10, hi = -10;
  gsize       i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      lo = MIN (lo, e[4 * i]);
      hi = MAX (hi, e[4 * i]);
    }
  report ("normalize_black_to_white", lo == 0.0f && fabsf (hi - 1.0f) < 1e-6, "%g to %g", lo, hi);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <wavelet-noise.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_filter ();
  test_reference ();
  test_known_noise ();
  test_normalize ();
  check_standard ("blocks_8", OP, NULL, 0);
  check_standard ("blocks_5", OP, "block-size", 5);
  check_standard ("normalized", OP, "normalize", TRUE);
  return check_end ();
}
