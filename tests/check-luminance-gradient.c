/*
 * Checks of forensics:luminance-gradient
 *
 * check-luminance-gradient.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-luminance-gradient <luminance-gradient.so>
 *
 * Linear ramps (a known gradient: the colors of each mode by their
 * formulas); flat images; a scene of spheres lit from the left with one
 * lit from the right pasted in (its gradient points the other way); a
 * pasted object with a sharp edge among soft ones; against a brute force
 * reference; pieces, NaN, alpha, formats, determinism.
 */

#include "check-common.h"

#define OP "forensics:luminance-gradient"

static inline gdouble
luma (const gfloat *p)
{
  gdouble v[3];
  gint    c;

  for (c = 0; c < 3; c++)
    v[c] = p[c] != p[c] ? 0 : p[c] > 1e6 ? 1 : p[c] < -1e6 ? 0 : p[c];
  return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}

static void
hsv (gdouble h, gdouble v, gdouble *rgb)
{
  gdouble h6 = (h - floor (h)) * 6, f = h6 - floor (h6);
  gint    i = (gint) h6 % 6;
  gdouble t[6][3] = { { v, v * f, 0 }, { v * (1 - f), v, 0 }, { 0, v, v * f },
                      { 0, v * (1 - f), v }, { v * f, 0, v }, { v, 0, v * (1 - f) } };

  rgb[0] = t[i][0]; rgb[1] = t[i][1]; rgb[2] = t[i][2];
}

static gfloat *
reference (const gfloat *img, gint w, gint h, gint mode, gdouble k)
{
  gfloat *out = g_new (gfloat, (gsize) w * h * 4);
  gint    x, y;

#define Y(xx, yy) luma (img + ((gsize) CLAMP (yy, 0, h - 1) * w + CLAMP (xx, 0, w - 1)) * 4)
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble gx = (Y (x + 1, y) - Y (x - 1, y)) / 2, gy = (Y (x, y + 1) - Y (x, y - 1)) / 2;
        gfloat *p = out + ((gsize) y * w + x) * 4;
        gdouble rgb[3];

        if (mode == 0)
          {
            gdouble nx = -gx * k, ny = -gy * k, n = 1 / sqrt (nx * nx + ny * ny + 1);

            rgb[0] = 0.5 + 0.5 * nx * n; rgb[1] = 0.5 + 0.5 * ny * n; rgb[2] = 0.5 + 0.5 * n;
          }
        else if (mode == 1)
          {
            gdouble a = atan2 (-gy, gx) / (2 * G_PI);

            hsv (a < 0 ? a + 1 : a, MIN (hypot (gx, gy) * k / 4, 1.0), rgb);
          }
        else
          rgb[0] = rgb[1] = rgb[2] = hypot (gx, gy) * k / 4;
        p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2];
        p[3] = img[((gsize) y * w + x) * 4 + 3];
      }
#undef Y
  return out;
}

static void
test_reference (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 2, 1 }, { 1, 3 }, { 9, 7 }, { 70, 45 } };
  gint    s, mode, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1];
      gfloat     *img = make_scene (w, h, 70 + s, 3.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (mode = 0; mode < 3; mode++)
        {
          gfloat *got  = run_op (buf, OP, "mode", mode, "intensity", 13.0, NULL);
          gfloat *want = reference (img, w, h, mode, 13.0);
          gdouble d    = max_abs_diff (got, want, (gsize) w * h * 4);

          n++;
          worst = MAX (worst, d);
          if (d > 1e-5)
            bad++;
          g_free (got);
          g_free (want);
        }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0, "%d cases, 1x1 to 70x45: largest "
          "difference %.2g", n, worst);
}

static void
test_ramps (void)
{
  const gint w = 40, h = 30;
  gfloat    *img = g_new (gfloat, (gsize) w * h * 4);
  gdouble    a = 0.004, b = -0.003;   /* Y' per pixel along x and y */
  GeglBuffer *buf;
  gfloat     *e0, *e1, *e2;
  gdouble    d0 = 0, d1 = 0, d2 = 0, want[3], mag;
  gint       x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = img + ((gsize) y * w + x) * 4;

        p[0] = p[1] = p[2] = 0.4 + a * x + b * y;
        p[3] = 1;
      }
  buf = buffer_from (img, w, h, WORK);
  e0 = run_op (buf, OP, "mode", 0, "intensity", 20.0, NULL);
  e1 = run_op (buf, OP, "mode", 1, "intensity", 20.0, NULL);
  e2 = run_op (buf, OP, "mode", 2, "intensity", 20.0, NULL);
  mag = hypot (a, b);
  for (y = 1; y < h - 1; y++)
    for (x = 1; x < w - 1; x++)
      {
        gsize   i = ((gsize) y * w + x) * 4;
        gdouble nx = -a * 20, ny = -b * 20, n = 1 / sqrt (nx * nx + ny * ny + 1);
        gdouble ang = atan2 (-b, a) / (2 * G_PI);

        d0 = MAX (d0, fabs (e0[i] - (0.5 + 0.5 * nx * n)));
        d0 = MAX (d0, fabs (e0[i + 1] - (0.5 + 0.5 * ny * n)));
        d0 = MAX (d0, fabs (e0[i + 2] - (0.5 + 0.5 * n)));
        hsv (ang < 0 ? ang + 1 : ang, mag * 5, want);
        d1 = MAX (d1, max_abs_diff (e1 + i, (gfloat[]) { want[0], want[1], want[2] }, 3));
        d2 = MAX (d2, fabs (e2[i] - mag * 5));
      }
  /* (the ramp's values are floats: a few 1e-7) */
  report ("linear_ramp_known_gradient", d0 < 1e-5 && d1 < 1e-5 && d2 < 1e-5,
          "normal map %.1g, hue %.1g, magnitude %.1g off", d0, d1, d2);
  g_free (e0); g_free (e1); g_free (e2);
  g_object_unref (buf);

  for (x = 0; x < w * h * 4; x++)
    img[x] = x % 4 == 3 ? 1.0f : 0.37f;
  buf = buffer_from (img, w, h, WORK);
  e0 = run_op (buf, OP, NULL);
  e2 = run_op (buf, OP, "mode", 2, NULL);
  d0 = d2 = 0;
  for (x = 0; x < w * h; x++)
    {
      d0 = MAX (d0, max_abs_diff (e0 + 4 * x, (gfloat[]) { 0.5f, 0.5f, 1.0f }, 3));
      d2 = MAX (d2, e2[4 * x]);
    }
  report ("flat_image", d0 == 0.0 && d2 == 0.0, "the normal map is 0.5 0.5 1, "
          "the magnitude 0");
  g_free (e0); g_free (e2);
  g_object_unref (buf);
  g_free (img);
}

/* Lambertian spheres on a gray ground, lit from the left (light
 * direction (-1, 0, 1)); one at (cx2, cy) lit from the right */
static gfloat *
spheres (gint w, gint h, gint odd_x, gint odd_y)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  gint    x, y, i, j;
  Rng     rng = { 9 };

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *q = p + ((gsize) y * w + x) * 4;
        gdouble v = 0.35;

        for (j = 0; j < 3; j++)
          for (i = 0; i < 4; i++)
            {
              gdouble cx = 64 + i * 128, cy = 64 + j * 128, r = 48;
              gdouble dx = (x - cx) / r, dy = (y - cy) / r, dz2 = 1 - dx * dx - dy * dy;

              if (dz2 > 0)
                {
                  gdouble lx = (i == odd_x && j == odd_y) ? 1 : -1;
                  gdouble l = (dx * lx + sqrt (dz2)) / sqrt (2.0);

                  v = 0.1 + 0.8 * MAX (l, 0.0);
                }
            }
        q[0] = q[1] = q[2] = CLAMP (v + 1.0 / 255 * rng_gauss (&rng), 0, 1);
        q[3] = 1;
      }
  return p;
}

static void
test_lighting (void)
{
  const gint  w = 512, h = 384;
  gfloat     *img = spheres (w, h, 2, 1);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e = run_op (buf, OP, NULL);
  gdouble     red[12];
  gint        i, j, bad = 0;

  /* the mean red of the normal map over each sphere: above 0.5 where
   * the image gets darker to the right (lit from the left) */
  for (j = 0; j < 3; j++)
    for (i = 0; i < 4; i++)
      {
        GeglRectangle r = { 64 + i * 128 - 30, 64 + j * 128 - 30, 60, 60 };

        red[j * 4 + i] = mean_in (e, w, h, &r, NULL, 0);
        if ((i == 2 && j == 1) != (red[j * 4 + i] < 0.5))
          bad++;
      }
  report ("sphere_lit_from_the_other_side", bad == 0,
          "normal map red over the spheres lit from the left %.3f to %.3f, "
          "over the one lit from the right %.3f",
          MIN (MIN (red[0], red[1]), red[3]), MAX (MAX (red[0], red[1]), red[3]),
          red[6]);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_sharp_edge (void)
{
  const gint  w = 256, h = 128;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
  GeglBuffer *buf;
  gfloat     *e;
  gdouble     soft = 0, sharp = 0;
  gint        x, y;

  /* two dark bars on light ground: the left one with a soft edge (a
   * ramp over 6 pixels, as a camera sees it), the right one pasted in
   * with a hard edge */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = img + ((gsize) y * w + x) * 4;
        gdouble v = 0.8;

        if (x < 128)
          v = 0.8 - 0.6 * CLAMP ((x - 40) / 6.0, 0.0, 1.0);
        else if (x >= 180)
          v = 0.2;
        p[0] = p[1] = p[2] = v;
        p[3] = 1;
      }
  buf = buffer_from (img, w, h, WORK);
  e = run_op (buf, OP, "mode", 2, NULL);
  for (x = 1; x < w - 1; x++)
    {
      gdouble v = e[((gsize) 64 * w + x) * 4];

      if (x < 120)
        soft = MAX (soft, v);
      else
        sharp = MAX (sharp, v);
    }
  report ("sharp_pasted_edge", sharp > 2.5 * soft,
          "largest gradient at the hard edge %.3f, at the soft one %.3f",
          sharp, soft);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_edges (void)
{
  const gint  w = 61, h = 43;
  gfloat     *img = make_scene (w, h, 71, 3.0), *clean;
  GeglBuffer *b1, *b2, *b8;
  gfloat     *e1, *e2, *e3, *e8, *ref8;
  gsize       i;
  gboolean    finite = TRUE;
  const gfloat bad[] = { NAN, INFINITY, -INFINITY };
  gint        threads;
  Rng         rng = { 12 };
  gint        k, pbad = 0;

  clean = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h * 4; i += 11)
    {
      gfloat v = bad[(i / 11) % G_N_ELEMENTS (bad)];

      img[i] = v;
      clean[i] = i % 4 == 3 ? (v == v ? v : 0) : (v == v && v > 0 ? 1.0f : 0.0f);
    }
  b1 = buffer_from (img, w, h, WORK);
  b2 = buffer_from (clean, w, h, WORK);
  e1 = run_op (b1, OP, NULL);
  e2 = run_op (b2, OP, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3 && ! isfinite (e1[i]))
      finite = FALSE;
  /* (alpha: NaN becomes 0, infinities stay) */
  for (i = 3; i < (gsize) w * h * 4; i += 4)
    if (e1[i] != e2[i] && ! (isinf (e1[i]) && isinf (e2[i])))
      finite = FALSE;
  report ("nan_and_infinities", finite, "finite colors, alpha as it is (NaN as 0)");
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 == 3)
      e1[i] = e2[i];
  report ("nan_same_as_clamped", max_abs_diff (e1, e2, (gsize) w * h * 4) == 0.0, NULL);

  g_object_get (gegl_config (), "threads", &threads, NULL);
  g_object_set (gegl_config (), "threads", 1, NULL);
  e3 = run_op (b2, OP, NULL);
  g_object_set (gegl_config (), "threads", threads, NULL);
  report ("deterministic", max_abs_diff (e2, e3, (gsize) w * h * 4) == 0.0, NULL);

  for (k = 0; k < 10; k++)
    {
      GeglRectangle r;
      gfloat       *got;
      gint          y;

      r.x = rng_next (&rng) % w;
      r.y = rng_next (&rng) % h;
      r.width  = 1 + rng_next (&rng) % (w - r.x);
      r.height = 1 + rng_next (&rng) % (h - r.y);
      got = run_op_on (b2, &r, OP, NULL);
      for (y = 0; y < r.height; y++)
        if (max_abs_diff (got + (gsize) y * r.width * 4,
                          e2 + ((gsize) (r.y + y) * w + r.x) * 4,
                          (gsize) r.width * 4) != 0.0)
          {
            pbad++;
            break;
          }
      g_free (got);
    }
  report ("pieces_same_as_whole_image", pbad == 0, "%d of 10 differ", pbad);

  for (i = 0; i < (gsize) w * h * 4; i++)
    clean[i] = i % 4 == 3 ? (gfloat) (i % 7) / 6 : roundf (clean[i] * 255) / 255;
  b8   = buffer_from (clean, w, h, "R'G'B'A u8");
  e8   = run_op (b8, OP, NULL);
  ref8 = reference (clean, w, h, 0, 8.0);
  report ("input_8_bit_and_alpha", max_abs_diff (e8, ref8, (gsize) w * h * 4) < 2e-3,
          "an 8 bit image with alpha: largest difference %.2g (alpha in 8 bit)",
          max_abs_diff (e8, ref8, (gsize) w * h * 4));
  g_free (e1); g_free (e2); g_free (e3); g_free (e8); g_free (ref8);
  g_object_unref (b1); g_object_unref (b2); g_object_unref (b8);
  g_free (img); g_free (clean);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <luminance-gradient.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_reference ();
  test_ramps ();
  test_lighting ();
  test_sharp_edge ();
  test_edges ();

  return check_end ();
}
