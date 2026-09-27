/*
 * Checks of forensics:minmax
 *
 * check-minmax.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-minmax <minmax.so>
 *
 * Against a brute force reference (strict 3 x 3 extrema of the 8 bit
 * channel, edge pixels never, the density in a window clipped to the
 * image), from 1 x 1 up; independent noise makes 1 pixel in 9 an
 * extremum each way; a blurred and an upscaled region have fewer; flat
 * images none; the standard checks.
 */

#include "check-common.h"

#define OP "forensics:minmax"

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
luma (const gfloat *p)
{
  return (u8 (p[0]) * 4899 + u8 (p[1]) * 9617 + u8 (p[2]) * 1868 + 8192) >> 14;
}

/* +1 maximum, -1 minimum, 0 else (edge pixels: 0) */
static gint *
extrema (const gfloat *img, gint w, gint h)
{
  gint *f = g_new0 (gint, (gsize) w * h);
  gint  x, y, i, j;

  for (y = 1; y < h - 1; y++)
    for (x = 1; x < w - 1; x++)
      {
        gint c = luma (img + ((gsize) y * w + x) * 4), lo = 256, hi = -1;

        for (j = -1; j <= 1; j++)
          for (i = -1; i <= 1; i++)
            if (i || j)
              {
                gint v = luma (img + ((gsize) (y + j) * w + x + i) * 4);

                lo = MIN (lo, v);
                hi = MAX (hi, v);
              }
        f[(gsize) y * w + x] = c < lo ? -1 : c > hi ? 1 : 0;
      }
  return f;
}

static gfloat *
reference (const gfloat *img, gint w, gint h, gint mode, gint window, gdouble gain)
{
  gint   *f = extrema (img, w, h);
  gfloat *out = g_new (gfloat, (gsize) w * h * 4);
  gint    before = (window - 1) / 2, after = window - 1 - before, x, y, i, j;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = out + ((gsize) y * w + x) * 4;

        if (mode == 0)
          {
            gint v = f[(gsize) y * w + x];

            p[0] = v > 0;
            p[1] = v < 0;
          }
        else
          {
            gint hi = 0, lo = 0, n = 0;

            for (j = MAX (y - before, 0); j <= MIN (y + after, h - 1); j++)
              for (i = MAX (x - before, 0); i <= MIN (x + after, w - 1); i++)
                {
                  hi += f[(gsize) j * w + i] > 0;
                  lo += f[(gsize) j * w + i] < 0;
                  n++;
                }
            p[0] = (gfloat) ((gdouble) hi / n) * (gfloat) gain;
            p[1] = (gfloat) ((gdouble) lo / n) * (gfloat) gain;
          }
        p[2] = 0;
        p[3] = img[((gsize) y * w + x) * 4 + 3];
      }
  g_free (f);
  return out;
}

static void
test_reference (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 2, 3 }, { 3, 3 }, { 9, 7 }, { 40, 31 }, { 97, 64 } };
  gint    s, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1], mode, window;
      gfloat     *img = make_scene (w, h, 30 + s, 6.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (mode = 0; mode < 2; mode++)
        for (window = 3; window <= (mode ? 16 : 3); window += 13)
          {
            gfloat *got = run_op (buf, OP, "mode", mode, "window", window, "gain", 3.0, NULL);
            gfloat *want = reference (img, w, h, mode, window, 3.0);
            gdouble d = max_abs_diff (got, want, (gsize) w * h * 4);

            n++;
            worst = MAX (worst, d);
            if (d > 1e-6)
              {
                bad++;
                printf ("      %dx%d mode %d window %d: %g\n", w, h, mode, window, d);
              }
            g_free (got);
            g_free (want);
          }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0, "%d cases (1x1 to 97x64, extrema, density in "
          "windows of 3 and 16): largest difference %.2g", n, worst);
}

static void
test_noise_share (void)
{
  const gint  w = 300, h = 300;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
  Rng         rng = { 17 };
  GeglBuffer *buf;
  gfloat     *out;
  gdouble     hi = 0, lo = 0;
  gsize       i;

  /* independent values over the whole range: ties are rare */
  for (i = 0; i < (gsize) w * h; i++)
    {
      gfloat v = (rng_next (&rng) % 256) / 255.0f;

      img[4 * i] = img[4 * i + 1] = img[4 * i + 2] = v;
      img[4 * i + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  out = run_op (buf, OP, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    {
      hi += out[4 * i];
      lo += out[4 * i + 1];
    }
  hi /= (gdouble) (w - 2) * (h - 2);
  lo /= (gdouble) (w - 2) * (h - 2);
  /* P (the largest of 9 distinct values is the centre) = 1/9; ties with
   * 256 levels lower it a little: (1 / 256) sum_k ((k / 256)^8) = 0.1092 */
  report ("independent_noise_one_in_nine", fabs (hi - 0.1092) < 0.004 && fabs (lo - 0.1092) < 0.004,
          "maxima %.4f, minima %.4f of the pixels (1/9 without ties, 0.1092 with 256 levels)",
          hi, lo);
  g_free (out);
  g_object_unref (buf);
  g_free (img);
}

static void
test_smoothed_regions (void)
{
  const gint    w = 384, h = 256;
  GeglRectangle r = { 128, 64, 128, 128 };
  gfloat       *img = make_texture (w, h, 4.0), *blur = g_memdup2 (img, (gsize) w * h * 16);
  GeglBuffer   *buf;
  gfloat       *out;
  gdouble       in = 0, rest = 0;
  gint          x, y, c, n_in = 0, n_rest = 0;

  /* a 3 x 3 box blur of the region */
  for (y = r.y; y < r.y + r.height; y++)
    for (x = r.x; x < r.x + r.width; x++)
      for (c = 0; c < 3; c++)
        {
          gdouble s = 0;
          gint    i, j;

          for (j = -1; j <= 1; j++)
            for (i = -1; i <= 1; i++)
              s += img[((gsize) (y + j) * w + x + i) * 4 + c];
          blur[((gsize) y * w + x) * 4 + c] = (gfloat) (s / 9);
        }
  buf = buffer_from (blur, w, h, WORK);
  out = run_op (buf, OP, "mode", 1, "window", 15, "gain", 1.0, NULL);
  for (y = 8; y < h - 8; y++)
    for (x = 8; x < w - 8; x++)
      {
        gdouble v = out[((gsize) y * w + x) * 4] + out[((gsize) y * w + x) * 4 + 1];
        gboolean inside = x >= r.x + 8 && x < r.x + r.width - 8 && y >= r.y + 8 &&
                          y < r.y + r.height - 8;
        gboolean away = x < r.x - 8 || x >= r.x + r.width + 8 || y < r.y - 8 ||
                        y >= r.y + r.height + 8;

        if (inside)
          {
            in += v;
            n_in++;
          }
        else if (away)
          {
            rest += v;
            n_rest++;
          }
      }
  in /= n_in;
  rest /= n_rest;
  report ("blurred_region_fewer_extrema", in < 0.5 * rest,
          "share of extrema: blurred region %.3f, the rest %.3f (x %.2f)", in, rest, in / rest);
  g_free (out);
  g_object_unref (buf);
  g_free (blur);
  g_free (img);
}

static void
test_flat (void)
{
  const gint  w = 40, h = 30;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
  GeglBuffer *buf;
  gfloat     *a, *b;
  gsize       i;
  gdouble     m = 0;

  for (i = 0; i < (gsize) w * h; i++)
    {
      img[4 * i] = 0.2f;
      img[4 * i + 1] = 0.5f;
      img[4 * i + 2] = 0.7f;
      img[4 * i + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  a = run_op (buf, OP, NULL);
  b = run_op (buf, OP, "mode", 1, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    m = MAX (m, MAX (a[4 * i] + a[4 * i + 1], b[4 * i] + b[4 * i + 1]));
  report ("flat_image_no_extrema", m == 0.0, NULL);
  g_free (a);
  g_free (b);
  g_object_unref (buf);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <minmax.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_reference ();
  test_noise_share ();
  test_smoothed_regions ();
  test_flat ();
  check_standard ("markers", OP, NULL, 0);
  check_standard ("density", OP, "mode", 1);
  return check_end ();
}
