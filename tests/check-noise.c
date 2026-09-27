/*
 * Checks of forensics:noise
 *
 * check-noise.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-noise <noise.so>
 *
 * A spliced region with more noise and one with less (airbrushed); the
 * residual of flat and linear images; the residual growing with the
 * noise; against a brute force reference (medians of 3 x 3 to 7 x 7, the
 * B3 spline, windows clipped to the image, repeated edge pixels); pieces;
 * sizes down to 1 x 1; determinism; alpha; NaN; formats; auto levels; the
 * invalidated region.
 */

#include "check-common.h"
#include <gegl-plugin.h>

#define OP "forensics:noise"

static gdouble
median_of (gdouble *v, gint n)
{
  qsort (v, n, sizeof *v, cmp_double);
  return v[n / 2];
}

static inline gdouble
sane (gdouble v)
{
  return v != v ? 0 : v > 1e6 ? 1 : v < -1e6 ? 0 : v;
}

/* the reference, in double precision, pixel by pixel */
static gfloat *
reference (const gfloat *img, gint w, gint h, gint method, gint radius,
           gint mode, gdouble amp, gint average)
{
  gint     nc = mode == 0 ? 3 : 1, x, y, c, i, j;
  gdouble *ch = g_new (gdouble, (gsize) w * h * nc);
  gdouble *r  = g_new (gdouble, (gsize) w * h * nc);
  gfloat  *out = g_new (gfloat, (gsize) w * h * 4);
  gint     before = average > 1 && mode != 2 ? (average - 1) / 2 : 0;
  gint     after  = average > 1 && mode != 2 ? average - 1 - before : 0;
  static const gdouble k[5] = { 1, 4, 6, 4, 1 };

  for (i = 0; i < w * h; i++)
    if (nc == 3)
      for (c = 0; c < 3; c++)
        ch[i * 3 + c] = sane (img[i * 4 + c]);
    else
      ch[i] = 0.2126 * sane (img[i * 4]) + 0.7152 * sane (img[i * 4 + 1]) +
              0.0722 * sane (img[i * 4 + 2]);
#define AT(xx, yy, cc) ch[((gsize) CLAMP (yy, 0, h - 1) * w + CLAMP (xx, 0, w - 1)) * nc + (cc)]
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      for (c = 0; c < nc; c++)
        {
          gdouble d;

          if (method == 1)
            {
              gdouble s = 0;

              for (j = -2; j <= 2; j++)
                for (i = -2; i <= 2; i++)
                  s += k[i + 2] * k[j + 2] / 256 * AT (x + i, y + j, c);
              d = AT (x, y, c) - s;
            }
          else
            {
              gdouble v[49];
              gint    n = 0;

              for (j = -radius; j <= radius; j++)
                for (i = -radius; i <= radius; i++)
                  v[n++] = AT (x + i, y + j, c);
              d = AT (x, y, c) - median_of (v, n);
            }
          r[((gsize) y * w + x) * nc + c] = d;
        }
#undef AT
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble e[3];

        for (c = 0; c < nc; c++)
          {
            if (mode == 2)
              e[c] = r[(gsize) y * w + x];
            else if (! before && ! after)
              e[c] = fabs (r[((gsize) y * w + x) * nc + c]);
            else
              {
                gdouble s = 0;
                gint    m = 0;

                for (j = MAX (y - before, 0); j <= MIN (y + after, h - 1); j++)
                  for (i = MAX (x - before, 0); i <= MIN (x + after, w - 1); i++)
                    {
                      s += fabs (r[((gsize) j * w + i) * nc + c]);
                      m++;
                    }
                e[c] = s / m;
              }
          }
        if (nc == 1)
          e[1] = e[2] = e[0];
        for (c = 0; c < 3; c++)
          out[((gsize) y * w + x) * 4 + c] = mode == 2 ? 0.5 + e[0] * amp : e[c] * amp;
        out[((gsize) y * w + x) * 4 + 3] = img[((gsize) y * w + x) * 4 + 3];
      }
  g_free (ch);
  g_free (r);
  return out;
}

static void
test_reference (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 2, 3 }, { 7, 5 }, { 33, 20 }, { 64, 41 } };
  gint    s, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1], method, radius, mode, avg;
      gfloat     *img = make_scene (w, h, 50 + s, 4.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (method = 0; method < 2; method++)
        for (radius = 1; radius <= (method ? 1 : 3); radius++)
          for (mode = 0; mode < 3; mode++)
            for (avg = 0; avg <= 9; avg += 9)
              {
                gfloat *got = run_op (buf, OP, "method", method, "radius", radius,
                                      "mode", mode, "amplitude", 7.0,
                                      "average", avg, NULL);
                gfloat *want = reference (img, w, h, method, radius, mode, 7.0, avg);
                gdouble d = max_abs_diff (got, want, (gsize) w * h * 4);

                n++;
                worst = MAX (worst, d);
                if (d > 2e-5)
                  {
                    bad++;
                    printf ("      %dx%d method %d radius %d mode %d average %d: %g\n",
                            w, h, method, radius, mode, avg, d);
                  }
                g_free (got);
                g_free (want);
              }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0,
          "%d cases (1x1 to 64x41, medians 3x3 to 7x7, wavelet, 3 modes, "
          "averaging): largest difference %.2g", n, worst);
}

/* the scene (or a smooth texture) with grain 2, and the region r from the
 * same with another grain */
static gfloat *
spliced (gint w, gint h, const GeglRectangle *r, gdouble grain, gboolean texture)
{
  gfloat *a = texture ? make_texture_of (w, h, 2.0, FALSE) : make_scene (w, h, 1, 2.0);
  gfloat *b = texture ? make_texture_of (w, h, grain, FALSE) : make_scene (w, h, 1, grain);
  gint    y;

  for (y = r->y; y < r->y + r->height; y++)
    memcpy (a + ((gsize) y * w + r->x) * 4, b + ((gsize) y * w + r->x) * 4,
            (gsize) r->width * 4 * sizeof (gfloat));
  g_free (b);
  return a;
}

/* 16 x 16 block means inside r and away from it */
static void
blocks (const gfloat *e, gint w, gint h, const GeglRectangle *r, gdouble **in,
        gsize *n_in, gdouble **out, gsize *n_out)
{
  gint bx, by;

  *in  = g_new (gdouble, (gsize) (w / 16) * (h / 16) + 1);
  *out = g_new (gdouble, (gsize) (w / 16) * (h / 16) + 1);
  *n_in = *n_out = 0;
  for (by = 0; by + 16 <= h; by += 16)
    for (bx = 0; bx + 16 <= w; bx += 16)
      {
        GeglRectangle b = { bx, by, 16, 16 };
        gboolean inside = bx >= r->x && bx + 16 <= r->x + r->width &&
                          by >= r->y && by + 16 <= r->y + r->height;
        gboolean away = bx + 32 <= r->x || bx >= r->x + r->width + 16 ||
                        by + 32 <= r->y || by >= r->y + r->height + 16;

        if (inside)
          (*in)[(*n_in)++] = mean_in (e, w, h, &b, NULL, -1);
        else if (away)
          (*out)[(*n_out)++] = mean_in (e, w, h, &b, NULL, -1);
      }
}

static void
splice_case (const gchar *name, gboolean texture, gdouble grain, gint method,
             gint mode, gint average)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 176, 112, 160, 128 };
  gfloat       *img = spliced (w, h, &r, grain, texture);
  GeglBuffer   *buf = buffer_from (img, w, h, WORK);
  gfloat       *e = run_op (buf, OP, "method", method, "mode", mode,
                            "average", average, NULL);
  gdouble      *in, *out, m_in = 0, m_out = 0, p1, p99, hit = 0;
  gsize         n_in, n_out, i;
  gboolean      louder = grain > 2.0;

  blocks (e, w, h, &r, &in, &n_in, &out, &n_out);
  for (i = 0; i < n_in; i++)
    m_in += in[i] / n_in;
  for (i = 0; i < n_out; i++)
    m_out += out[i] / n_out;
  p1  = quantile (out, n_out, 0.01);
  p99 = quantile (out, n_out, 0.99);
  for (i = 0; i < n_in; i++)
    hit += (louder ? in[i] > p99 : in[i] < p1) ? 1.0 / n_in : 0;
  /* On the texture only the noise differs: at a threshold at the 99th
   * (1st) percentile of the blocks away from the region, 1 % of them are
   * flagged by construction, and nine in ten of the region's blocks must
   * be. On the photo-like scene its fine texture and edges vary more
   * from block to block than the noise: there the region's mean must
   * differ (x 1.5 or 1 / 1.5), block by block it is only reported (the
   * finest wavelet level keeps more of the texture than the median) */
  report (name, texture ? hit >= 0.9
                        : (louder ? m_in >= 1.5 * m_out : m_in * 1.5 <= m_out),
          "%s, grain %.1f in grain 2: region %.4f, elsewhere %.4f (x %.2f); "
          "%.0f %% of its blocks beyond the %s percentile elsewhere",
          texture ? "texture" : "scene", grain, m_in, m_out, m_in / m_out,
          100 * hit, louder ? "99th" : "1st");
  g_free (in);
  g_free (out);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_splices (void)
{
  splice_case ("texture_noisier_region_median", TRUE, 6.0, 0, 0, 0);
  splice_case ("texture_noisier_region_wavelet_luminance", TRUE, 6.0, 1, 1, 0);
  splice_case ("texture_noisier_region_averaged", TRUE, 4.0, 0, 1, 16);
  splice_case ("texture_airbrushed_region_median", TRUE, 0.0, 0, 0, 0);
  splice_case ("texture_airbrushed_region_averaged", TRUE, 0.5, 1, 1, 16);
  splice_case ("scene_noisier_region_median", FALSE, 6.0, 0, 0, 0);
  splice_case ("scene_noisier_region_wavelet_luminance", FALSE, 6.0, 1, 1, 0);
  splice_case ("scene_noisier_region_averaged", FALSE, 4.0, 0, 1, 16);
  splice_case ("scene_airbrushed_region_median", FALSE, 0.0, 0, 0, 0);
  splice_case ("scene_airbrushed_region_averaged", FALSE, 0.0, 0, 0, 16);
}

static void
test_simple_images (void)
{
  const gint w = 40, h = 30;
  gfloat    *flat = g_new (gfloat, (gsize) w * h * 4), *ramp = g_new (gfloat, (gsize) w * h * 4);
  gint       x, y, method;
  gdouble    mf = 0, mr = 0;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *f = flat + ((gsize) y * w + x) * 4, *p = ramp + ((gsize) y * w + x) * 4;

        f[0] = 0.3f; f[1] = 0.5f; f[2] = 0.7f; f[3] = 1;
        p[0] = 0.01f * x; p[1] = 0.005f * y; p[2] = 0.3f + 0.004f * (x + y); p[3] = 1;
      }
  for (method = 0; method < 2; method++)
    {
      GeglBuffer *bf = buffer_from (flat, w, h, WORK), *br = buffer_from (ramp, w, h, WORK);
      gfloat     *ef = run_op (bf, OP, "method", method, NULL);
      gfloat     *er = run_op (br, OP, "method", method, NULL);

      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            mf = MAX (mf, max_abs_diff (ef + ((gsize) y * w + x) * 4,
                                        (gfloat[]) { 0, 0, 0 }, 3));
            /* away from the edges, where the repeated pixels bend it */
            if (x >= 3 && x < w - 3 && y >= 3 && y < h - 3)
              mr = MAX (mr, max_abs_diff (er + ((gsize) y * w + x) * 4,
                                          (gfloat[]) { 0, 0, 0 }, 3));
          }
      g_free (ef);
      g_free (er);
      g_object_unref (bf);
      g_object_unref (br);
    }
  report ("flat_and_linear_images_no_noise", mf == 0.0 && mr < 1e-5,
          "median and wavelet: flat %g, linear ramp %.2g", mf, mr);
  g_free (flat);
  g_free (ramp);
}

static void
test_grows_with_noise (void)
{
  const gint w = 256, h = 256;
  gdouble    m[3];
  gint       k, method, bad = 0;

  for (method = 0; method < 2; method++)
    {
      for (k = 0; k < 3; k++)
        {
          gdouble     sigma = 1.0 + k;  /* 1, 2, 3 levels */
          gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
          Rng         rng = { 77 };
          GeglBuffer *buf;
          gfloat     *e;
          gsize       i;

          for (i = 0; i < (gsize) w * h * 4; i++)
            img[i] = i % 4 == 3 ? 1.0f : 0.5f + sigma / 255 * rng_gauss (&rng);
          buf = buffer_from (img, w, h, WORK);
          e = run_op (buf, OP, "method", method, "amplitude", 1.0, NULL);
          m[k] = mean_in (e, w, h, &(GeglRectangle) { 0, 0, w, h }, NULL, -1);
          g_free (e);
          g_object_unref (buf);
          g_free (img);
        }
      /* for Gaussian noise the residual is proportional to sigma */
      if (fabs (m[1] / m[0] - 2) > 0.1 || fabs (m[2] / m[0] - 3) > 0.15)
        bad++;
      printf ("      (%s: mean residual %.3f, %.3f, %.3f levels for sigma 1, 2, 3)\n",
              method ? "wavelet" : "median", m[0] * 255, m[1] * 255, m[2] * 255);
    }
  report ("residual_proportional_to_the_noise", bad == 0,
          "Gaussian noise of 1, 2 and 3 levels: ratios within 5 %%");
}

static void
test_edges (void)
{
  const gint  w = 50, h = 37;
  gfloat     *img = make_scene (w, h, 60, 3.0), *clean, *half;
  GeglBuffer *b1, *b2, *b3;
  gfloat     *e1, *e2, *e3, *e4;
  gsize       i;
  gboolean    finite = TRUE;
  gdouble     da = 0;
  const gfloat bad[] = { NAN, INFINITY, -INFINITY };
  gint        threads;

  clean = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  half  = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h * 4; i += 9)
    if (i % 4 != 3)
      {
        gfloat v = bad[(i / 9) % G_N_ELEMENTS (bad)];

        img[i] = v;
        clean[i] = v == v && v > 0 ? 1.0f : 0.0f;
      }
  for (i = 0; i < (gsize) w * h; i++)
    half[4 * i + 3] = (i % w) / (gfloat) w;
  b1 = buffer_from (img, w, h, WORK);
  b2 = buffer_from (clean, w, h, WORK);
  b3 = buffer_from (half, w, h, WORK);
  e1 = run_op (b1, OP, "average", 5, NULL);
  e2 = run_op (b2, OP, "average", 5, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (! isfinite (e1[i]))
      finite = FALSE;
  report ("nan_and_infinities", finite && max_abs_diff (e1, e2, (gsize) w * h * 4) == 0.0,
          "finite, as with NaN as 0, infinity as 1 and minus infinity as 0");
  e3 = run_op (b3, OP, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    da = MAX (da, fabs (e3[4 * i + 3] - half[4 * i + 3]));
  report ("alpha_passed_through", da == 0.0, NULL);
  g_object_get (gegl_config (), "threads", &threads, NULL);
  g_object_set (gegl_config (), "threads", 1, NULL);
  e4 = run_op (b2, OP, "average", 5, NULL);
  g_object_set (gegl_config (), "threads", threads, NULL);
  report ("deterministic", max_abs_diff (e2, e4, (gsize) w * h * 4) == 0.0,
          "with 1 and %d threads", threads);
  g_free (e1); g_free (e2); g_free (e3); g_free (e4);
  g_object_unref (b1); g_object_unref (b2); g_object_unref (b3);
  g_free (img); g_free (clean); g_free (half);
}

static void
test_formats_and_pieces (void)
{
  const gint  w = 91, h = 67;
  gfloat     *img = make_scene (w, h, 61, 3.0);
  gsize       i;
  static const gchar *formats[] = { "R'G'B'A u8", "R'G'B'A u16", "RGBA float" };
  GeglBuffer *ref;
  gfloat     *want;
  gint        f, bad = 0;
  Rng         rng = { 3 };

  /* 8 bit values, so that every format holds them exactly */
  for (i = 0; i < (gsize) w * h * 4; i++)
    img[i] = roundf (img[i] * 255) / 255;
  ref  = buffer_from (img, w, h, WORK);
  want = run_op (ref, OP, "average", 7, NULL);
  for (f = 0; f < (gint) G_N_ELEMENTS (formats); f++)
    {
      GeglBuffer *b = buffer_from (img, w, h, formats[f]);
      gfloat     *got = run_op (b, OP, "average", 7, NULL);
      gdouble     d = max_abs_diff (got, want, (gsize) w * h * 4);

      if (d > 1e-5)
        {
          bad++;
          printf ("      %s: %g\n", formats[f], d);
        }
      g_free (got);
      g_object_unref (b);
    }
  report ("input_formats", bad == 0, "8 bit, 16 bit and linear float input");

  bad = 0;
  for (i = 0; i < 12; i++)
    {
      GeglRectangle r;
      gfloat       *got;
      gint          y;

      r.x = rng_next (&rng) % w;
      r.y = rng_next (&rng) % h;
      r.width  = 1 + rng_next (&rng) % (w - r.x);
      r.height = 1 + rng_next (&rng) % (h - r.y);
      got = run_op_on (ref, &r, OP, "average", 7, NULL);
      for (y = 0; y < r.height; y++)
        if (max_abs_diff (got + (gsize) y * r.width * 4,
                          want + ((gsize) (r.y + y) * w + r.x) * 4,
                          (gsize) r.width * 4) != 0.0)
          {
            bad++;
            break;
          }
      g_free (got);
    }
  report ("pieces_same_as_whole_image", bad == 0, "%d of 12 differ", bad);
  g_free (want);
  g_object_unref (ref);
  g_free (img);
}

static void
test_auto_levels (void)
{
  const gint  w = 200, h = 150;
  gfloat     *img = make_scene (w, h, 62, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e = run_op (buf, OP, "auto-levels", TRUE, NULL);
  gsize       i, white = 0, n = (gsize) w * h * 3;

  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3)
      white += e[i] >= 1.0f;
  report ("auto_levels_white_at_99_4_percent", white >= 0.004 * n && white <= 0.008 * n,
          "%.2f %% of the values white", 100.0 * white / n);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

static void
test_invalidated (void)
{
  const gint    w = 90, h = 70;
  gfloat       *img = make_scene (w, h, 63, 3.0);
  gfloat       *changed = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  GeglBuffer   *b1 = buffer_from (img, w, h, WORK), *b2;
  GeglRectangle px = { 40, 30, 1, 1 }, inv;
  GeglNode     *graph = gegl_node_new ();
  GeglNode     *src = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                                           "buffer", b1, NULL);
  GeglNode     *node = gegl_node_new_child (graph, "operation", OP, "radius", 2,
                                            "average", 6, NULL);
  gfloat       *e1, *e2;
  gint          x, y, bad = 0;

  gegl_node_link (src, node);
  gegl_node_blit (node, 1.0, &(GeglRectangle) { 0, 0, w, h }, NULL, NULL, 0,
                  GEGL_BLIT_DEFAULT);
  inv = gegl_operation_get_invalidated_by_change (gegl_node_get_gegl_operation (node),
                                                  "input", &px);
  g_object_unref (graph);
  changed[(30 * w + 40) * 4] = 1.0f - changed[(30 * w + 40) * 4];
  b2 = buffer_from (changed, w, h, WORK);
  e1 = run_op (b1, OP, "radius", 2, "average", 6, NULL);
  e2 = run_op (b2, OP, "radius", 2, "average", 6, NULL);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      if (max_abs_diff (e1 + ((gsize) y * w + x) * 4, e2 + ((gsize) y * w + x) * 4, 4) != 0.0 &&
          ! (x >= inv.x && x < inv.x + inv.width && y >= inv.y && y < inv.y + inv.height))
        bad++;
  report ("changes_within_invalidated_region", bad == 0, "%d outside", bad);
  g_free (e1); g_free (e2); g_free (changed); g_free (img);
  g_object_unref (b1); g_object_unref (b2);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <noise.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_reference ();
  test_splices ();
  test_simple_images ();
  test_grows_with_noise ();
  test_edges ();
  test_formats_and_pieces ();
  test_auto_levels ();
  test_invalidated ();

  return check_end ();
}
