/*
 * Checks of forensics:median-detect
 *
 * check-median-detect.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-median-detect <median-detect.so>
 *
 * A region of a textured image filtered with a 3 x 3 and a 5 x 5 median
 * is marked red and the rest is not; blurring is not taken for a median;
 * the same after a JPEG save at 90 (where the method is weak: the numbers
 * are reported, and the check is that the save hides the region); flat
 * images are blue; the ratio against a brute force count; the standard
 * checks.
 */

#include "check-jpeg.h"

#define OP "forensics:median-detect"

static gint
u8 (gfloat v)
{
  if (! (v > 0.0f))
    return 0;
  if (v >= 1.0f)
    return 255;
  return (gint) (v * 255.0f + 0.5f);
}

static int
cmp_int (const void *a, const void *b)
{
  return *(const gint *) a - *(const gint *) b;
}

/* the (2r + 1)^2 median of the 8 bit values inside r, per channel */
static void
median_region (gfloat *img, gint w, gint h, const GeglRectangle *r, gint radius)
{
  gfloat *src = g_memdup2 (img, (gsize) w * h * 16);
  gint    x, y, c, i, j;

  for (y = r->y; y < r->y + r->height; y++)
    for (x = r->x; x < r->x + r->width; x++)
      for (c = 0; c < 3; c++)
        {
          gint v[49], n = 0;

          for (j = -radius; j <= radius; j++)
            for (i = -radius; i <= radius; i++)
              v[n++] = u8 (src[((gsize) CLAMP (y + j, 0, h - 1) * w + CLAMP (x + i, 0, w - 1)) * 4 + c]);
          qsort (v, n, sizeof *v, cmp_int);
          img[((gsize) y * w + x) * 4 + c] = v[n / 2] / 255.0f;
        }
  g_free (src);
}

static void
blur_region (gfloat *img, gint w, gint h, const GeglRectangle *r)
{
  gfloat *src = g_memdup2 (img, (gsize) w * h * 16);
  gint    x, y, c, i, j;

  for (y = r->y; y < r->y + r->height; y++)
    for (x = r->x; x < r->x + r->width; x++)
      for (c = 0; c < 3; c++)
        {
          gdouble s = 0;

          for (j = -1; j <= 1; j++)
            for (i = -1; i <= 1; i++)
              s += src[((gsize) CLAMP (y + j, 0, h - 1) * w + CLAMP (x + i, 0, w - 1)) * 4 + c];
          img[((gsize) y * w + x) * 4 + c] = roundf ((gfloat) (s / 9) * 255) / 255;
        }
  g_free (src);
}

/* the share of red among the judged (not blue) 32 x 32 blocks inside r and
 * away from it */
static void
red_shares (const gfloat *m, gint w, gint h, const GeglRectangle *r, gdouble *in, gdouble *out,
            gint *judged_in)
{
  gint bx, by, n_in = 0, n_out = 0, red_in = 0, red_out = 0;

  for (by = 0; by + 32 <= h; by += 32)
    for (bx = 0; bx + 32 <= w; bx += 32)
      {
        const gfloat *p = m + ((gsize) (by + 16) * w + bx + 16) * 4;
        gboolean inside = bx >= r->x && bx + 32 <= r->x + r->width && by >= r->y &&
                          by + 32 <= r->y + r->height;
        gboolean away = bx + 32 <= r->x || bx >= r->x + r->width || by + 32 <= r->y ||
                        by >= r->y + r->height;

        if (p[2] > 0.5f)
          continue;                 /* blue: too flat */
        if (inside)
          {
            n_in++;
            red_in += p[0] > 0.5f;
          }
        else if (away)
          {
            n_out++;
            red_out += p[0] > 0.5f;
          }
      }
  *in = n_in ? (gdouble) red_in / n_in : 0;
  *out = n_out ? (gdouble) red_out / n_out : 0;
  *judged_in = n_in;
}

static void
test_median_regions (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 160, 96, 192, 192 };
  gint          radius;

  for (radius = 1; radius <= 2; radius++)
    {
      gfloat     *img = make_texture_of (w, h, 4.0, FALSE);
      GeglBuffer *buf;
      gfloat     *m;
      gdouble     in, out;
      gint        judged;
      gchar      *name;

      median_region (img, w, h, &r, radius);
      buf = buffer_from (img, w, h, WORK);
      m = run_op (buf, OP, NULL);
      red_shares (m, w, h, &r, &in, &out, &judged);
      name = g_strdup_printf ("median_%dx%d_region_found", 2 * radius + 1, 2 * radius + 1);
      report (name, judged >= 10 && in >= 0.9 && out <= 0.02,
              "red: %.0f %% of the region's %d judged blocks, %.1f %% of the others", 100 * in,
              judged, 100 * out);
      g_free (name);
      g_free (m);
      g_object_unref (buf);

      if (radius == 1)
        {
          /* the same, saved as JPEG at 90 */
          guint8 *q = to_u8 (img, w, h), *j = jpeg_file (q, w, h, 90, FX_CHROMA_420);
          gfloat *jf = from_u8 (j, w, h);
          gdouble jin, jout;

          buf = buffer_from (jf, w, h, WORK);
          m = run_op (buf, OP, NULL);
          red_shares (m, w, h, &r, &jin, &jout, &judged);
          report ("jpeg_90_hides_it", jin < in,
                  "the known weakness: after a JPEG save at 90, red in %.0f %% of the region's "
                  "judged blocks, %.1f %% of the others", 100 * jin, 100 * jout);
          g_free (m);
          g_object_unref (buf);
          g_free (jf);
          g_free (j);
          g_free (q);
        }
      g_free (img);
    }
}

static void
test_blur_not_median (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle r = { 160, 96, 192, 192 };
  gfloat       *img = make_texture_of (w, h, 4.0, FALSE);
  GeglBuffer   *buf;
  gfloat       *m;
  gdouble       in, out;
  gint          judged;

  blur_region (img, w, h, &r);
  buf = buffer_from (img, w, h, WORK);
  m = run_op (buf, OP, NULL);
  red_shares (m, w, h, &r, &in, &out, &judged);
  report ("blur_is_not_median", in <= 0.1,
          "a 3 x 3 box blur instead: red in %.0f %% of the region's %d judged blocks", 100 * in,
          judged);
  g_free (m);
  g_object_unref (buf);
  g_free (img);
}

static void
test_flat_and_ratio (void)
{
  const gint  w = 100, h = 70;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4), *m, *scene, *ratio;
  GeglBuffer *buf;
  gsize       i;
  gint        blue = 1, bx, by, bad = 0, x, y;

  for (i = 0; i < (gsize) w * h; i++)
    {
      img[4 * i] = img[4 * i + 1] = img[4 * i + 2] = 0.4f;
      img[4 * i + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  m = run_op (buf, OP, NULL);
  for (i = 0; i < (gsize) w * h; i++)
    blue &= m[4 * i] == 0 && m[4 * i + 1] == 0 && m[4 * i + 2] == 1;
  report ("flat_image_blue", blue, NULL);
  g_free (m);
  g_object_unref (buf);

  scene = make_scene (w, h, 12, 4.0);
  buf = buffer_from (scene, w, h, WORK);
  ratio = run_op (buf, OP, "mode", 1, "block-size", 16, "gain", 1.0, NULL);
  for (by = 0; by < h; by += 16)
    for (bx = 0; bx < w; bx += 16)
      {
        gint    h0 = 0, h1 = 0;
        gdouble want;

        for (y = by; y < MIN (by + 16, h); y++)
          for (x = bx; x < MIN (bx + 16, w); x++)
            {
              gint c, k;

              for (c = 0; c < 3; c++)
                for (k = 0; k < 2; k++)
                  {
                    gint nx = x + (k == 0), ny = y + (k == 1), d;

                    if (nx >= MIN (bx + 16, w) || ny >= MIN (by + 16, h))
                      continue;
                    d = abs (u8 (scene[((gsize) ny * w + nx) * 4 + c]) -
                             u8 (scene[((gsize) y * w + x) * 4 + c]));
                    h0 += d == 0;
                    h1 += d == 1;
                  }
            }
        want = (h0 + 1.0) / (h1 + 1.0) * 0.5;
        for (y = by; y < MIN (by + 16, h); y++)
          for (x = bx; x < MIN (bx + 16, w); x++)
            if (fabs (ratio[((gsize) y * w + x) * 4] - want) > 1e-6)
              bad++;
      }
  report ("ratio_same_as_count", bad == 0, "h0 / h1 per 16 x 16 block (R, G and B), counted here: %d pixels "
          "differ", bad);
  g_free (ratio);
  g_object_unref (buf);
  g_free (scene);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <median-detect.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_median_regions ();
  test_blur_not_median ();
  test_flat_and_ratio ();
  check_standard ("map", OP, NULL, 0);
  check_standard ("ratio_blocks_8", OP, "block-size", 8);
  return check_end ();
}
