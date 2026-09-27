/*
 * Checks of forensics:clone-detect
 *
 * check-clone-detect.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-clone-detect <clone-detect.so>
 *
 * A cloned patch is found (also off the block grid, and after a JPEG
 * save); an image without clones is clean; flat areas are not matched
 * (and would be without the minimal detail); a 12 megapixel image with a
 * clone, found after the reduction, in a time limit; images smaller than
 * a block; the overlay's colors; NaN, alpha, determinism.
 */

#include "check-jpeg.h"

#define OP "forensics:clone-detect"

/* copies the w0 x h0 patch at (sx, sy) to (dx, dy) */
static void
clone_patch (gfloat *img, gint w, gint sx, gint sy, gint w0, gint h0, gint dx, gint dy)
{
  gint y;

  for (y = 0; y < h0; y++)
    memmove (img + ((gsize) (dy + y) * w + dx) * 4, img + ((gsize) (sy + y) * w + sx) * 4,
             (gsize) w0 * 4 * sizeof (gfloat));
}

/* the share of the mask's pixels in r that are white */
static gdouble
covered (const gfloat *mask, gint w, gint h, const GeglRectangle *r)
{
  return mean_in (mask, w, h, r, NULL, 0);
}

/* the share of white pixels away from the rectangles (by margin) */
static gdouble
stray (const gfloat *mask, gint w, gint h, const GeglRectangle *a,
       const GeglRectangle *b, gint margin)
{
  gsize n = 0, white = 0;
  gint  x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gboolean near = FALSE;
        const GeglRectangle *rs[2] = { a, b };
        gint     k;

        for (k = 0; k < 2; k++)
          if (rs[k] && x >= rs[k]->x - margin && x < rs[k]->x + rs[k]->width + margin &&
              y >= rs[k]->y - margin && y < rs[k]->y + rs[k]->height + margin)
            near = TRUE;
        if (near)
          continue;
        n++;
        white += mask[((gsize) y * w + x) * 4] > 0.5f;
      }
  return n ? (gdouble) white / n : 0;
}

static void
clone_case (const gchar *name, gint sx, gint sy, gint dx, gint dy, gint jpeg,
            gdouble min_cover)
{
  const gint    w = 640, h = 480, size = 64;
  gfloat       *img = make_scene (w, h, 3, 2.0);
  GeglRectangle src = { sx, sy, size, size }, dst = { dx, dy, size, size };
  GeglBuffer   *buf;
  gfloat       *mask;
  gdouble       cs, cd, st;

  clone_patch (img, w, sx, sy, size, size, dx, dy);
  if (jpeg)
    {
      guint8 *u8 = to_u8 (img, w, h), *j = jpeg_file (u8, w, h, jpeg, FX_CHROMA_420);
      gfloat *f = from_u8 (j, w, h);

      g_free (img);
      img = f;
      g_free (u8);
      g_free (j);
    }
  buf  = buffer_from (img, w, h, WORK);
  mask = run_op (buf, OP, "mode", 1, NULL);
  cs = covered (mask, w, h, &src);
  cd = covered (mask, w, h, &dst);
  st = stray (mask, w, h, &src, &dst, 16);
  report (name, cs >= min_cover && cd >= min_cover && st <= 0.001,
          "64 x 64 patch copied %d, %d pixels%s: %.0f %% of the original and "
          "%.0f %% of the copy marked, %.3f %% of the rest", dx - sx, dy - sy,
          jpeg ? g_strdup_printf (", then saved at JPEG quality %d", jpeg) : "",
          100 * cs, 100 * cd, 100 * st);
  g_free (mask);
  g_object_unref (buf);
  g_free (img);
}

static void
test_clones (void)
{
  /* on the textured ground; the second one off the 16 pixel grid */
  clone_case ("cloned_patch_found", 80, 300, 400, 336, 0, 0.8);
  clone_case ("cloned_patch_off_grid_found", 83, 297, 411, 339, 0, 0.8);
  /* (JPEG changes the copy and the original differently: fewer of their
   * blocks stay equal after the quantisation) */
  clone_case ("cloned_patch_after_jpeg_90_found", 80, 300, 400, 336, 90, 0.4);
}

static void
test_clean (void)
{
  const gint  w = 640, h = 480;
  gfloat     *img = make_scene (w, h, 3, 2.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *mask = run_op (buf, OP, "mode", 1, NULL);
  gdouble     st = stray (mask, w, h, NULL, NULL, 0);

  report ("no_clone_nothing_marked", st == 0.0, "%.4f %% marked", 100 * st);
  g_free (mask);
  g_object_unref (buf);
  g_free (img);
}

static void
test_flat_and_edges (void)
{
  const gint  w = 640, h = 480;
  gfloat     *img = make_scene (w, h, 3, 2.0);
  GeglBuffer *buf;
  gfloat     *def, *none;
  GeglRectangle top = { 0, 0, w, h / 2 - 24 }, edge = { 0, h / 2 - 24, w, 48 };
  Rng         rng = { 5 };
  gint        x, y;

  /* the upper half flat, with a little noise (a clear sky), ending in a
   * straight edge across the whole image */
  for (y = 0; y < h / 2; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = img + ((gsize) y * w + x) * 4;

        p[0] = 0.45f + 0.5f / 255 * rng_gauss (&rng);
        p[1] = 0.62f + 0.5f / 255 * rng_gauss (&rng);
        p[2] = 0.9f + 0.5f / 255 * rng_gauss (&rng);
      }
  buf  = buffer_from (img, w, h, WORK);
  def  = run_op (buf, OP, "mode", 1, NULL);
  none = run_op (buf, OP, "mode", 1, "min-detail", 0.0, NULL);
  /* flat blocks differ by their noise, which is all of their detail:
   * the comparison of their pixels leaves them out even without the
   * minimal detail; the blocks of the straight edge match each other
   * along it and are left out as matching themselves */
  report ("flat_area_not_matched",
          covered (def, w, h, &top) == 0.0 && covered (none, w, h, &top) == 0.0,
          "a flat sky: %.2f %% marked, %.2f %% with the minimal detail 0",
          100 * covered (def, w, h, &top), 100 * covered (none, w, h, &top));
  report ("straight_edge_not_matched", covered (def, w, h, &edge) == 0.0 &&
          stray (def, w, h, NULL, NULL, 0) == 0.0,
          "a straight edge across the image: %.2f %% marked, %.3f %% of all",
          100 * covered (def, w, h, &edge), 100 * stray (def, w, h, NULL, NULL, 0));
  g_free (def);
  g_free (none);
  g_object_unref (buf);
  g_free (img);
}

static void
test_12_megapixels (void)
{
  const gint    w = 4000, h = 3000, size = 256;
  gfloat       *img = make_scene (w, h, 4, 2.0);
  GeglRectangle src = { 500, 2000, size, size }, dst = { 2600, 2200, size, size };
  GeglBuffer   *buf;
  gfloat       *mask;
  gint64        t0;
  gdouble       secs;

  clone_patch (img, w, src.x, src.y, size, size, dst.x, dst.y);
  buf  = buffer_from (img, w, h, WORK);
  t0   = g_get_monotonic_time ();
  mask = run_op (buf, OP, "mode", 1, NULL);
  secs = (g_get_monotonic_time () - t0) / 1e6;
  /* the default analysis size 1536: reduced by 3 (1333 x 1000), the
   * patch is 85 pixels there. (The generated sky holds one real near
   * copy of its own, 1980 pixels across: its value noise repeats there,
   * 1.8 levels apart, the grain's own difference; it is found too and is
   * part of "the rest".) */
  report ("12_megapixels", covered (mask, w, h, &src) >= 0.8 &&
          covered (mask, w, h, &dst) >= 0.8 &&
          stray (mask, w, h, &src, &dst, 48) <= 0.001 && secs < 20.0,
          "4000 x 3000, a 256 x 256 clone: %.0f %% and %.0f %% marked, "
          "%.3f %% of the rest; %.2f s (limit 20 s, under the sanitizers too)",
          100 * covered (mask, w, h, &src), 100 * covered (mask, w, h, &dst),
          100 * stray (mask, w, h, &src, &dst, 48), secs);
  g_free (mask);
  g_object_unref (buf);
  g_free (img);
}

static void
test_small_and_edges (void)
{
  static const gint sizes[][2] = { { 1, 1 }, { 5, 5 }, { 15, 40 }, { 16, 16 }, { 33, 17 } };
  gint     s, bad = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1];
      gfloat     *img = make_scene (w, h, 5, 2.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);
      gfloat     *mask = run_op (buf, OP, "mode", 1, NULL);
      gfloat     *over = run_op (buf, OP, NULL);
      gsize       i;

      for (i = 0; i < (gsize) w * h; i++)
        if (mask[4 * i] != 0.0f ||
            fabsf (over[4 * i] - 0.35f * img[4 * i]) > 1e-6f)
          bad++;
      g_free (mask);
      g_free (over);
      g_object_unref (buf);
      g_free (img);
    }
  report ("images_smaller_than_blocks", bad == 0,
          "1x1 to 33x17: nothing marked, the image darkened");

  {
    const gint  w = 320, h = 240;
    gfloat     *img = make_scene (w, h, 6, 2.0), *half;
    GeglBuffer *b1, *b2;
    gfloat     *e1, *e2, *e3;
    gsize       i;
    gboolean    ok = TRUE;
    gint        threads;
    const gfloat bad_v[] = { NAN, INFINITY, -INFINITY };

    clone_patch (img, w, 20, 150, 48, 48, 200, 160);
    half = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
    for (i = 0; i < (gsize) w * h; i++)
      half[4 * i + 3] = (i % w) / (gfloat) w;
    for (i = 0; i < 300; i++)
      half[(i * 7919) % ((gsize) w * h * 4)] = bad_v[i % 3];
    b1 = buffer_from (img, w, h, WORK);
    b2 = buffer_from (half, w, h, WORK);
    e1 = run_op (b1, OP, NULL);
    g_object_get (gegl_config (), "threads", &threads, NULL);
    g_object_set (gegl_config (), "threads", 1, NULL);
    e2 = run_op (b1, OP, NULL);
    g_object_set (gegl_config (), "threads", threads, NULL);
    report ("deterministic", max_abs_diff (e1, e2, (gsize) w * h * 4) == 0.0, NULL);
    e3 = run_op (b2, OP, NULL);
    for (i = 0; i < (gsize) w * h * 4; i++)
      {
        if (i % 4 != 3 && ! isfinite (e3[i]))
          ok = FALSE;
        if (i % 4 == 3 && e3[i] != (half[i] == half[i] ? half[i] : 0.0f))
          ok = FALSE;
      }
    report ("nan_and_alpha", ok, "finite colors, alpha as it is (NaN as 0)");
    g_free (e1); g_free (e2); g_free (e3);
    g_object_unref (b1); g_object_unref (b2);
    g_free (img); g_free (half);
  }
}

static void
test_overlay (void)
{
  const gint  w = 640, h = 480;
  gfloat     *img = make_scene (w, h, 3, 2.0);
  GeglBuffer *buf;
  gfloat     *over, *nolines;
  GeglRectangle a = { 100, 310, 40, 40 }, b = { 420, 350, 40, 40 };
  gdouble     ca[3], cb[3], lit = 0;
  gint        c;
  gsize       i;

  clone_patch (img, w, 90, 300, 64, 64, 410, 340);
  buf     = buffer_from (img, w, h, WORK);
  over    = run_op (buf, OP, "lines", FALSE, NULL);
  nolines = run_op (buf, OP, NULL);
  for (c = 0; c < 3; c++)
    {
      ca[c] = mean_in (over, w, h, &a, NULL, c);
      cb[c] = mean_in (over, w, h, &b, NULL, c);
    }
  for (i = 0; i < (gsize) w * h * 4; i++)
    lit += over[i] != nolines[i];
  /* the original and the copy tinted alike (the color of their shift) */
  report ("overlay_colors_pair", fabs (ca[0] - cb[0]) < 0.05 && fabs (ca[1] - cb[1]) < 0.05 &&
          fabs (ca[2] - cb[2]) < 0.05 && lit > 0,
          "original %.2f %.2f %.2f, copy %.2f %.2f %.2f; the line changes %.0f values",
          ca[0], ca[1], ca[2], cb[0], cb[1], cb[2], lit);
  g_free (over);
  g_free (nolines);
  g_object_unref (buf);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <clone-detect.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_clones ();
  test_clean ();
  test_flat_and_edges ();
  test_small_and_edges ();
  test_overlay ();
  test_12_megapixels ();

  return check_end ();
}
