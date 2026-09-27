/*
 * Checks of forensics:jpeg-ghost
 *
 * check-jpeg-ghost.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-jpeg-ghost <jpeg-ghost.so>
 *
 * Farid's experiment (a region saved at a lower quality, pasted in, the
 * whole saved again): the ghost at the region's quality, with the
 * two-sample Kolmogorov-Smirnov statistic of Farid's paper, and the map
 * of the best quality; an untouched image; a region off the grid; a
 * cropped image and the grid offset; against a reference written here
 * (whole-image round trips, a brute force window average, the
 * normalisation of equation 4); pieces; sizes down to 1 x 1; window
 * sizes; sweeps in any order; the color ramp; alpha, NaN, formats,
 * determinism, the invalidated region.
 */

#include "check-jpeg.h"
#include <gegl-plugin.h>

#define OP "forensics:jpeg-ghost"

/* the reference: equation 3 with a centred window clipped to the image,
 * equation 4 over the sweep (the shown quality included); mode 0
 * normalised, 1 root of the difference x scale / 255, 2 best quality */
static gfloat *
reference (const gfloat *img, gint w, gint h, gint q_shown, gint q0, gint q1,
           gint step, gint b, gint mode, gdouble scale, gint chroma, gint ox,
           gint oy)
{
  guint8  *u8 = to_u8 (img, w, h);
  gint     qs[110], n = 0, q, k, x, y, shown = -1;
  gdouble *delta;
  gfloat  *out = g_new (gfloat, (gsize) w * h * 4);
  gint     before = (b - 1) / 2, after = b - 1 - before;

  for (q = MIN (q0, q1); q <= MAX (q0, q1); q += step)
    qs[n++] = q;
  for (k = 0; k < n; k++)
    if (qs[k] == q_shown)
      shown = k;
  if (shown < 0)
    {
      for (k = n; k > 0 && qs[k - 1] > q_shown; k--)
        qs[k] = qs[k - 1];
      qs[k] = q_shown;
      shown = k;
      n++;
    }
  delta = g_new (gdouble, (gsize) n * w * h);
  for (k = 0; k < n; k++)
    {
      guint8  *c  = canvas_round_trip (u8, w, h, qs[k], chroma, ox, oy);
      gint64  *sq = g_new (gint64, (gsize) w * h);

      for (x = 0; x < w * h; x++)
        {
          gint64 s = 0;
          gint   i;

          for (i = 0; i < 3; i++)
            s += (u8[3 * x + i] - c[3 * x + i]) * (u8[3 * x + i] - c[3 * x + i]);
          sq[x] = s;
        }
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            gint64  s = 0;
            gint    m = 0, i, j;

            for (j = MAX (y - before, 0); j <= MIN (y + after, h - 1); j++)
              for (i = MAX (x - before, 0); i <= MIN (x + after, w - 1); i++)
                {
                  s += sq[j * w + i];
                  m++;
                }
            delta[(gsize) k * w * h + y * w + x] = (gdouble) s / (3.0 * m);
          }
      g_free (sq);
      g_free (c);
    }
  for (x = 0; x < w * h; x++)
    {
      gdouble lo = G_MAXDOUBLE, hi = -1, v;
      gint    best = 0;

      for (k = 0; k < n; k++)
        {
          v = delta[(gsize) k * w * h + x];
          if (v < lo)
            {
              lo = v;
              best = k;
            }
          hi = MAX (hi, v);
        }
      v = delta[(gsize) shown * w * h + x];
      if (mode == 0)
        v = hi > lo ? (v - lo) / (hi - lo) : 0;
      else if (mode == 1)
        v = sqrt (v) / 255 * scale;
      else
        v = qs[n - 1] > qs[0] ? (gdouble) (qs[best] - qs[0]) / (qs[n - 1] - qs[0]) : 0;
      out[4 * x] = out[4 * x + 1] = out[4 * x + 2] = v;
      out[4 * x + 3] = img[4 * x + 3];
    }
  g_free (delta);
  g_free (u8);
  return out;
}

static gfloat *
ghost (GeglBuffer *buf, gint q, gint q0, gint q1, gint step, gint mode, gint ox,
       gint oy)
{
  return run_op (buf, OP, "quality", q, "sweep-min", q0, "sweep-max", q1,
                 "sweep-step", step, "mode", mode, "grid-x", ox, "grid-y", oy,
                 NULL);
}

/* the two-sample Kolmogorov-Smirnov statistic (Farid's equation 5) of
 * channel 0 inside r and outside it (every second pixel each way) */
static gdouble
ks (const gfloat *e, gint w, gint h, const GeglRectangle *r)
{
  gdouble *a = g_new (gdouble, (gsize) w * h), *b = g_new (gdouble, (gsize) w * h);
  gsize    na = 0, nb = 0, i = 0, j = 0;
  gdouble  d = 0;
  gint     x, y;

  for (y = 0; y < h; y += 2)
    for (x = 0; x < w; x += 2)
      {
        gboolean in = x >= r->x && x < r->x + r->width && y >= r->y &&
                      y < r->y + r->height;

        if (in)
          a[na++] = e[((gsize) y * w + x) * 4];
        else
          b[nb++] = e[((gsize) y * w + x) * 4];
      }
  qsort (a, na, sizeof *a, cmp_double);
  qsort (b, nb, sizeof *b, cmp_double);
  while (i < na && j < nb)
    {
      gdouble v = MIN (a[i], b[j]);

      while (i < na && a[i] <= v)
        i++;
      while (j < nb && b[j] <= v)
        j++;
      d = MAX (d, fabs ((gdouble) i / na - (gdouble) j / nb));
    }
  g_free (a);
  g_free (b);
  return d;
}

/* Farid's experiment: the scene saved at q1 with the region r taken
 * from a copy saved at q0 on a grid shifted by (dx, dy) */
static guint8 *
tampered (gint w, gint h, const GeglRectangle *r, gint q0, gint q1, gint dx,
          gint dy)
{
  gfloat *scene = make_scene (w, h, 1, 2.0);
  guint8 *s8 = to_u8 (scene, w, h);
  guint8 *low = dx || dy ? jpeg_shifted (s8, w, h, dx, dy, q0)
                         : jpeg_file (s8, w, h, q0, FX_CHROMA_420);
  guint8 *f;

  paste (s8, low, w, r);
  f = jpeg_file (s8, w, h, q1, FX_CHROMA_420);
  g_free (low);
  g_free (s8);
  g_free (scene);
  return f;
}

static void
test_farid (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 160, 112, 192, 160 };
  GeglRectangle all = { 0, 0, w, h };
  guint8       *f = tampered (w, h, &r, 60, 85, 0, 0);
  gfloat       *fi = from_u8 (f, w, h);
  GeglBuffer   *buf = buffer_from (fi, w, h, WORK);
  gfloat       *e;
  gdouble       k60, k_other = 0, in, out;
  gint          q;

  /* the ghost at the region's quality: dark in the region */
  e   = ghost (buf, 60, 50, 95, 5, 0, 0, 0);
  k60 = ks (e, w, h, &r);
  in  = mean_in (e, w, h, &r, NULL, 0);
  out = mean_in (e, w, h, &all, &r, 0);
  g_free (e);
  /* and at the other qualities of the sweep below the file's quality the
   * region does not stand out like that */
  for (q = 50; q <= 80; q += 10)
    {
      gdouble k;

      if (q == 60)
        continue;
      e = ghost (buf, q, 50, 95, 5, 0, 0, 0);
      k = ks (e, w, h, &r);
      k_other = MAX (k_other, mean_in (e, w, h, &r, NULL, 0) <
                              mean_in (e, w, h, &all, &r, 0) ? k : 0);
      g_free (e);
    }
  printf ("      (quality 60: region %.3f, elsewhere %.3f, K-S %.3f; darkest "
          "other quality K-S %.3f)\n", in, out, k60, k_other);
  /* K-S 0.5: half of the region's distribution apart from the rest.
   * Farid's own forgeries score 0.84 and 0.92 (section IV), an untouched
   * image here 0.11 (test_untouched) */
  report ("farid_ghost_at_the_region_quality", in < out && k60 > 0.5,
          "region saved at 60, the whole at 85, ghost at 60: normalized "
          "difference %.3f in the region, %.3f elsewhere, K-S %.2f",
          in, out, k60);

  /* the best quality of a sweep below the file's quality: the region's
   * earlier quality, the file's own elsewhere (the top of the sweep) */
  e = ghost (buf, 60, 40, 80, 5, 2, 0, 0);
  {
    gdouble *v = g_new (gdouble, (gsize) w * h), *u = g_new (gdouble, (gsize) w * h);
    gsize    nv = 0, nu = 0;
    gint     x, y;
    gdouble  qin, qout;

    for (y = r.y + 8; y < r.y + r.height - 8; y++)
      for (x = r.x + 8; x < r.x + r.width - 8; x++)
        v[nv++] = 40 + 40 * e[((gsize) y * w + x) * 4];
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
        if (x < r.x - 16 || x >= r.x + r.width + 16 || y < r.y - 16 ||
            y >= r.y + r.height + 16)
          u[nu++] = 40 + 40 * e[((gsize) y * w + x) * 4];
    qin  = quantile (v, nv, 0.5);
    qout = quantile (u, nu, 0.5);
    report ("farid_best_quality_map", fabs (qin - 60) < 0.5 && fabs (qout - 80) < 0.5,
            "sweep 40 to 80: the median best quality is %.0f in the region "
            "and %.0f elsewhere", qin, qout);
    g_free (v);
    g_free (u);
  }
  g_free (e);
  g_object_unref (buf);
  g_free (fi);
  g_free (f);
}

static void
test_untouched (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 160, 112, 192, 160 };
  gfloat       *scene = make_scene (w, h, 1, 2.0);
  guint8       *s8 = to_u8 (scene, w, h);
  guint8       *f = jpeg_file (s8, w, h, 85, FX_CHROMA_420);
  gfloat       *fi = from_u8 (f, w, h);
  GeglBuffer   *buf = buffer_from (fi, w, h, WORK);
  gdouble       kmax = 0;
  gint          q;

  for (q = 50; q <= 80; q += 10)
    {
      gfloat *e = ghost (buf, q, 50, 95, 5, 0, 0, 0);

      kmax = MAX (kmax, ks (e, w, h, &r));
      g_free (e);
    }
  /* the same region of an untouched image differs from the rest only
   * by its content */
  report ("untouched_image_no_ghost", kmax < 0.25,
          "saved once at 85: the largest K-S statistic of the same region "
          "at qualities 50 to 80 is %.2f", kmax);
  g_object_unref (buf);
  g_free (fi);
  g_free (f);
  g_free (s8);
  g_free (scene);
}

static void
test_grid (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 160, 112, 192, 160 };
  guint8       *f = tampered (w, h, &r, 60, 85, 4, 4);
  gfloat       *fi = from_u8 (f, w, h);
  GeglBuffer   *buf = buffer_from (fi, w, h, WORK);
  gfloat       *e = ghost (buf, 60, 50, 95, 5, 0, 0, 0);
  gdouble       k = ks (e, w, h, &r);

  /* Farid: a region moved off its 8 x 8 lattice loses its ghost */
  report ("off_grid_region_loses_the_ghost", k < 0.35,
          "the region saved at 60 on a grid 4 pixels off: K-S %.2f", k);
  g_free (e);
  g_object_unref (buf);
  g_free (fi);
  g_free (f);

  /* the tampered image cropped by 4 pixels: with the grid offset 12 the
   * ghost is back */
  {
    guint8       *t = tampered (w, h, &r, 60, 85, 0, 0);
    gint          cw = w - 4, ch = h - 4, y;
    guint8       *c = g_new (guint8, (gsize) cw * ch * 3);
    gfloat       *ci;
    GeglBuffer   *cb;
    GeglRectangle rc = { r.x - 4, r.y - 4, r.width, r.height };
    gfloat       *e0, *e12;
    gdouble       k0, k12;

    for (y = 0; y < ch; y++)
      memcpy (c + (gsize) y * cw * 3, t + ((gsize) (y + 4) * w + 4) * 3,
              (gsize) cw * 3);
    ci  = from_u8 (c, cw, ch);
    cb  = buffer_from (ci, cw, ch, WORK);
    e0  = ghost (cb, 60, 50, 95, 5, 0, 0, 0);
    e12 = ghost (cb, 60, 50, 95, 5, 0, 12, 12);
    k0  = ks (e0, cw, ch, &rc);
    k12 = ks (e12, cw, ch, &rc);
    report ("cropped_image_with_grid_offset", k12 > 0.5 && k12 > k0 + 0.2,
            "cropped by 4: K-S %.2f with offset 12, %.2f with 0", k12, k0);
    g_free (e0);
    g_free (e12);
    g_object_unref (cb);
    g_free (ci);
    g_free (c);
    g_free (t);
  }
}

static void
test_reference (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 5, 3 }, { 17, 9 }, { 40, 33 }, { 131, 77 } };
  gint s, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1], mode, b;
      gfloat     *img = make_scene (w, h, 20 + s, 3.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (mode = 0; mode < 3; mode++)
        for (b = 1; b <= 24; b += 7)
          {
            gint    chroma = (mode + b) % 3, ox = (b * 3) % 16, oy = (b * 5) % 16;
            gfloat *got = run_op (buf, OP, "quality", 65, "sweep-min", 40,
                                  "sweep-max", 90, "sweep-step", 10,
                                  "block-size", b, "mode", mode, "scale", 10.0,
                                  "chroma", chroma, "grid-x", ox, "grid-y", oy,
                                  NULL);
            gfloat *want = reference (img, w, h, 65, 40, 90, 10, b, mode, 10.0,
                                      chroma, ox, oy);
            gdouble d = max_abs_diff (got, want, (gsize) w * h * 4);

            n++;
            worst = MAX (worst, d);
            if (d > 1e-5)
              {
                bad++;
                printf ("      %dx%d mode %d window %d: %g\n", w, h, mode, b, d);
              }
            g_free (got);
            g_free (want);
          }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_reference", bad == 0,
          "%d cases (sizes 1x1 to 131x77, 3 modes, windows 1 to 22, the "
          "shown quality 65 outside the sweep 40..90): largest difference %.2g",
          n, worst);
}

static void
test_pieces (void)
{
  gint        w = 203, h = 151, i, bad = 0;
  gfloat     *img = make_scene (w, h, 31, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *want = ghost (buf, 70, 50, 95, 5, 0, 3, 7);
  Rng         rng = { 5 };

  for (i = 0; i < 12; i++)
    {
      GeglRectangle r;
      gfloat       *got;
      gint          y;

      r.x = rng_next (&rng) % w;
      r.y = rng_next (&rng) % h;
      r.width  = 1 + rng_next (&rng) % (w - r.x);
      r.height = 1 + rng_next (&rng) % (h - r.y);
      got = run_op_on (buf, &r, OP, "grid-x", 3, "grid-y", 7, NULL);
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
  report ("pieces_same_as_whole_image", bad == 0, "%d of 12 random rectangles differ", bad);
  g_free (want);
  g_object_unref (buf);
  g_free (img);
}

static void
test_options (void)
{
  const gint  w = 64, h = 48;
  gfloat     *img = make_scene (w, h, 40, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *a, *b, *c;
  gsize       i;
  gboolean    in_range = TRUE, ramp_ok = TRUE;

  /* the sweep in either order, and a step past its end */
  a = run_op (buf, OP, "sweep-min", 90, "sweep-max", 50, NULL);
  b = run_op (buf, OP, "sweep-min", 50, "sweep-max", 90, NULL);
  c = run_op (buf, OP, "sweep-min", 50, "sweep-max", 60, "sweep-step", 50, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3 && ! (b[i] >= 0.0f && b[i] <= 1.0f && c[i] >= 0.0f && c[i] <= 1.0f))
      in_range = FALSE;
  report ("sweep_in_either_order", max_abs_diff (a, b, (gsize) w * h * 4) == 0.0, NULL);
  report ("normalized_between_0_and_1", in_range, "also with one quality and the shown one");
  g_free (a);
  g_free (b);
  g_free (c);

  /* the color ramp: blue at 0, green at 0.5, red at 1 */
  a = run_op (buf, OP, "colormap", FALSE, NULL);
  b = run_op (buf, OP, "colormap", TRUE, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    {
      gfloat v = a[4 * i], *p = b + 4 * i;
      gfloat r = v > 0.5f ? (v - 0.5f) * 2 : 0, g = v > 0.5f ? 1 - (v - 0.5f) * 2 : v * 2;
      gfloat bl = v > 0.5f ? 0 : 1 - v * 2;

      if (fabsf (p[0] - r) > 1e-6f || fabsf (p[1] - g) > 1e-6f || fabsf (p[2] - bl) > 1e-6f)
        ramp_ok = FALSE;
    }
  report ("color_ramp", ramp_ok, "blue, green, red for 0, 0.5, 1");
  g_free (a);
  g_free (b);
  g_object_unref (buf);
  g_free (img);
}

static void
test_edges (void)
{
  const gint  w = 40, h = 30;
  gfloat     *img = make_scene (w, h, 41, 3.0), *clean, *half;
  GeglBuffer *b1, *b2, *b3;
  gfloat     *e1, *e2, *e3, *e4;
  gsize       i;
  gboolean    finite = TRUE;
  gdouble     da = 0;
  const gfloat bad[] = { NAN, INFINITY, -INFINITY, 2.0f, -1.0f };
  gint        threads;

  clean = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  half  = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h * 4; i += 5)
    if (i % 4 != 3)
      {
        gfloat v = bad[(i / 5) % G_N_ELEMENTS (bad)];

        img[i] = v;
        clean[i] = v == v && v > 0 ? MIN (v, 1.0f) : 0.0f;
      }
  for (i = 0; i < (gsize) w * h; i++)
    half[4 * i + 3] = (i % w) / (gfloat) w;
  b1 = buffer_from (img, w, h, WORK);
  b2 = buffer_from (clean, w, h, WORK);
  b3 = buffer_from (half, w, h, "R'G'B'A u16");
  e1 = run_op (b1, OP, NULL);
  e2 = run_op (b2, OP, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (! isfinite (e1[i]))
      finite = FALSE;
  report ("nan_and_infinities", finite && max_abs_diff (e1, e2, (gsize) w * h * 4) == 0.0,
          "finite, as with NaN as 0 and the rest clamped");
  e3 = run_op (b3, OP, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    da = MAX (da, fabs (e3[4 * i + 3] - half[4 * i + 3]));
  report ("alpha_passed_through", da < 1e-4, "largest difference %.2g (16 bit)", da);
  g_object_get (gegl_config (), "threads", &threads, NULL);
  g_object_set (gegl_config (), "threads", 1, NULL);
  e4 = run_op (b2, OP, NULL);
  g_object_set (gegl_config (), "threads", threads, NULL);
  report ("deterministic", max_abs_diff (e2, e4, (gsize) w * h * 4) == 0.0,
          "with 1 and %d threads", threads);
  g_free (e1); g_free (e2); g_free (e3); g_free (e4);
  g_object_unref (b1); g_object_unref (b2); g_object_unref (b3);
  g_free (img); g_free (clean); g_free (half);
}

static void
test_invalidated (void)
{
  const gint    w = 120, h = 90;
  gfloat       *img = make_scene (w, h, 42, 3.0);
  gfloat       *changed = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  GeglBuffer   *b1 = buffer_from (img, w, h, WORK), *b2;
  GeglRectangle px = { 60, 40, 1, 1 }, inv;
  GeglNode     *graph = gegl_node_new ();
  GeglNode     *src = gegl_node_new_child (graph, "operation", "gegl:buffer-source",
                                           "buffer", b1, NULL);
  GeglNode     *node = gegl_node_new_child (graph, "operation", OP, "block-size", 9,
                                            "grid-x", 5, NULL);
  gfloat       *e1, *e2;
  gint          x, y, bad = 0;

  gegl_node_link (src, node);
  gegl_node_blit (node, 1.0, &(GeglRectangle) { 0, 0, w, h }, NULL, NULL, 0,
                  GEGL_BLIT_DEFAULT);
  inv = gegl_operation_get_invalidated_by_change (gegl_node_get_gegl_operation (node),
                                                  "input", &px);
  g_object_unref (graph);
  changed[(40 * w + 60) * 4 + 1] = 1.0f - changed[(40 * w + 60) * 4 + 1];
  b2 = buffer_from (changed, w, h, WORK);
  /* the difference itself (normalised values move with far pixels'
   * sweeps only through their own window) */
  e1 = run_op (b1, OP, "block-size", 9, "grid-x", 5, "mode", 1, NULL);
  e2 = run_op (b2, OP, "block-size", 9, "grid-x", 5, "mode", 1, NULL);
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
      fprintf (stderr, "usage: %s <jpeg-ghost.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_farid ();
  test_untouched ();
  test_grid ();
  test_reference ();
  test_pieces ();
  test_options ();
  test_edges ();
  test_invalidated ();

  return check_end ();
}
