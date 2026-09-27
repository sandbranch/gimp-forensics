/*
 * Checks of forensics:error-level
 *
 * check-error-level.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-error-level <error-level.so>
 *
 * Against a reference written here (a JPEG round trip of the whole image
 * on its padded canvas, then the difference), on sizes from 1 x 1 up, all
 * chroma subsamplings and grid offsets, and on pieces of the image; a
 * forensic case (a JPEG image with a region pasted in and saved again);
 * uniform error on a never compressed texture; quality 100; the grid
 * offset (4 pixels, 8 pixels, a cropped image); determinism; alpha; NaN
 * and infinities; 8 bit, 16 bit, float, linear and gray input; auto
 * levels; the output modes; GEGL's own jpg-save and jpg-load; the region
 * a change invalidates. Prints PASS or FAIL per check.
 */

#include "check-common.h"
#include <gegl-plugin.h>
#include "forensics-jpeg.h"

#define OP "forensics:error-level"

/* 8 bit R'G'B' of an R'G'B'A float image, as the operation reads it */
static guint8 *
to_u8 (const gfloat *p, gint w, gint h)
{
  guint8 *q = g_new (guint8, (gsize) w * h * 3);
  gsize   i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      q[3 * i + 0] = fx_to_u8 (p[4 * i + 0]);
      q[3 * i + 1] = fx_to_u8 (p[4 * i + 1]);
      q[3 * i + 2] = fx_to_u8 (p[4 * i + 2]);
    }
  return q;
}

static gfloat *
from_u8 (const guint8 *q, gint w, gint h)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  gsize   i;

  for (i = 0; i < (gsize) w * h; i++)
    {
      p[4 * i + 0] = q[3 * i + 0] / 255.0f;
      p[4 * i + 1] = q[3 * i + 1] / 255.0f;
      p[4 * i + 2] = q[3 * i + 2] / 255.0f;
      p[4 * i + 3] = 1.0f;
    }
  return p;
}

/* a JPEG round trip of a whole 8 bit image (a file saved and opened) */
static guint8 *
jpeg_file (const guint8 *rgb, gint w, gint h, gint quality, gint chroma)
{
  guint8 *out = g_new (guint8, (gsize) w * h * 3);

  if (! fx_jpeg_round_trip (rgb, w, h, quality, chroma, out))
    g_error ("libjpeg failed");
  return out;
}

/* the reference: the whole image on its canvas (edge pixels copied into
 * the partial blocks left and above the grid), one round trip, the
 * difference; out is R'G'B'A float with alpha from the image */
static gfloat *
reference (const gfloat *img, gint w, gint h, gint quality, gint chroma,
           gint ox, gint oy, gdouble scale, gint mode)
{
  gint    px = (16 - ox % 16) % 16, py = (16 - oy % 16) % 16;
  gint    cw = w + px, ch = h + py, x, y, c;
  guint8 *u8 = to_u8 (img, w, h);
  guint8 *canvas = g_new (guint8, (gsize) cw * ch * 3), *copy;
  gfloat *out = g_new (gfloat, (gsize) w * h * 4);
  gfloat  gain = (gfloat) (scale / 255.0);

  for (y = 0; y < ch; y++)
    for (x = 0; x < cw; x++)
      {
        gint sx = MAX (x - px, 0), sy = MAX (y - py, 0);

        memcpy (canvas + ((gsize) y * cw + x) * 3, u8 + ((gsize) sy * w + sx) * 3, 3);
      }
  copy = jpeg_file (canvas, cw, ch, quality, chroma);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        const guint8 *a = canvas + ((gsize) (y + py) * cw + x + px) * 3;
        const guint8 *b = copy + ((gsize) (y + py) * cw + x + px) * 3;
        gfloat       *o = out + ((gsize) y * w + x) * 4;
        gfloat        e[3];

        if (mode == 0)
          for (c = 0; c < 3; c++)
            e[c] = abs (a[c] - b[c]);
        else if (mode == 1)
          {
            gfloat ya = 0.299f * a[0] + 0.587f * a[1] + 0.114f * a[2];
            gfloat yb = 0.299f * b[0] + 0.587f * b[1] + 0.114f * b[2];

            e[0] = e[1] = e[2] = fabsf (ya - yb);
          }
        else
          e[0] = e[1] = e[2] = MAX (MAX (abs (a[0] - b[0]), abs (a[1] - b[1])),
                                    abs (a[2] - b[2]));
        for (c = 0; c < 3; c++)
          o[c] = e[c] * gain;
        o[3] = img[((gsize) y * w + x) * 4 + 3];
      }
  g_free (u8);
  g_free (canvas);
  g_free (copy);
  return out;
}

static gfloat *
ela (GeglBuffer *in, gint quality, gint chroma, gint ox, gint oy)
{
  return run_op (in, OP, "quality", quality, "chroma", chroma,
                 "grid-x", ox, "grid-y", oy, "scale", 20.0, NULL);
}

/* 1. the operation is the reference, exactly -------------------------- */
static void
test_reference (void)
{
  static const gint sizes[][2] = {
    { 1, 1 }, { 3, 2 }, { 7, 7 }, { 8, 8 }, { 9, 17 }, { 17, 9 }, { 16, 16 },
    { 33, 31 }, { 64, 48 }, { 131, 77 }, { 300, 200 } };
  static const gint offsets[][2] = { { 0, 0 }, { 4, 4 }, { 3, 11 }, { 15, 8 } };
  gint    s, c, k, bad = 0, n = 0;
  gdouble worst = 0;

  for (s = 0; s < (gint) G_N_ELEMENTS (sizes); s++)
    {
      gint        w = sizes[s][0], h = sizes[s][1];
      gfloat     *img = make_scene (w, h, 11 + s, 3.0);
      GeglBuffer *buf = buffer_from (img, w, h, WORK);

      for (c = 0; c < 3; c++)
        for (k = 0; k < (gint) G_N_ELEMENTS (offsets); k++)
          {
            gint    ox = offsets[k][0], oy = offsets[k][1];
            gfloat *got  = ela (buf, 90, c, ox, oy);
            gfloat *want = reference (img, w, h, 90, c, ox, oy, 20.0, 0);
            gdouble d    = max_abs_diff (got, want, (gsize) w * h * 4);

            n++;
            if (d != 0.0)
              {
                bad++;
                printf ("      %dx%d chroma %d offset %d,%d: max difference %g\n",
                        w, h, c, ox, oy, d);
              }
            worst = MAX (worst, d);
            g_free (got);
            g_free (want);
          }
      g_object_unref (buf);
      g_free (img);
    }
  report ("same_as_whole_image_round_trip", bad == 0,
          "%d sizes from 1x1 to 300x200, 3 subsamplings, 4 grid offsets: "
          "%d of %d differ, largest difference %g", (gint) G_N_ELEMENTS (sizes),
          bad, n, worst);
}

/* pieces of the image (as GIMP asks for them) give the same pixels */
static void
test_pieces (void)
{
  gint        w = 301, h = 203, c, i, bad = 0;
  gfloat     *img = make_scene (w, h, 5, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  Rng         rng = { 99 };

  for (c = 0; c < 3; c++)
    {
      gfloat *want = reference (img, w, h, 85, c, 5, 13, 20.0, 0);

      for (i = 0; i < 12; i++)
        {
          GeglRectangle r;
          gfloat       *got;
          gint          y;

          r.x = rng_next (&rng) % w;
          r.y = rng_next (&rng) % h;
          r.width  = 1 + rng_next (&rng) % (w - r.x);
          r.height = 1 + rng_next (&rng) % (h - r.y);
          got = run_op_on (buf, &r, OP, "quality", 85, "chroma", c,
                           "grid-x", 5, "grid-y", 13, NULL);
          for (y = 0; y < r.height; y++)
            if (max_abs_diff (got + (gsize) y * r.width * 4,
                              want + ((gsize) (r.y + y) * w + r.x) * 4,
                              (gsize) r.width * 4) != 0.0)
              {
                bad++;
                printf ("      piece %d,%d %dx%d differs (chroma %d)\n",
                        r.x, r.y, r.width, r.height, c);
                break;
              }
          g_free (got);
        }
      g_free (want);
    }
  report ("pieces_same_as_whole_image", bad == 0,
          "36 random rectangles of a 301x203 image, grid offset 5,13");
  g_object_unref (buf);
  g_free (img);
}

/* 2. the forensic case ------------------------------------------------ */

/* 16 x 16 block means of the mean of R, G, B of an ELA result, for the
 * blocks inside r (inside = TRUE) or those not touching r */
static gdouble *
block_means (const gfloat *e, gint w, gint h, const GeglRectangle *r,
             gboolean inside, gsize *n)
{
  gdouble *v = g_new (gdouble, (gsize) (w / 16) * (h / 16) + 1);
  gint     bx, by;

  *n = 0;
  for (by = 0; by + 16 <= h; by += 16)
    for (bx = 0; bx + 16 <= w; bx += 16)
      {
        gboolean in  = bx >= r->x && bx + 16 <= r->x + r->width &&
                       by >= r->y && by + 16 <= r->y + r->height;
        gboolean out = bx + 16 <= r->x || bx >= r->x + r->width ||
                       by + 16 <= r->y || by >= r->y + r->height;
        GeglRectangle b = { bx, by, 16, 16 };

        if (inside ? in : out)
          v[(*n)++] = mean_in (e, w, h, &b, NULL, -1);
      }
  return v;
}

/* a region taken from the scene src, placed into the JPEG image at r */
static void
paste (guint8 *dst, const guint8 *src, gint w, const GeglRectangle *r)
{
  gint y;

  for (y = r->y; y < r->y + r->height; y++)
    memcpy (dst + ((gsize) y * w + r->x) * 3, src + ((gsize) y * w + r->x) * 3,
            (gsize) r->width * 3);
}

/* src saved as a JPEG with its blocks shifted by (dx, dy): a copy made
 * from a crop, or pasted at another position */
static guint8 *
jpeg_shifted (const guint8 *src, gint w, gint h, gint dx, gint dy, gint q)
{
  gint    cw = w + dx, ch = h + dy, x, y;
  guint8 *c = g_new (guint8, (gsize) cw * ch * 3), *j, *out;

  for (y = 0; y < ch; y++)
    for (x = 0; x < cw; x++)
      memcpy (c + ((gsize) y * cw + x) * 3,
              src + ((gsize) MAX (y - dy, 0) * w + MAX (x - dx, 0)) * 3, 3);
  j   = jpeg_file (c, cw, ch, q, FX_CHROMA_420);
  out = g_new (guint8, (gsize) w * h * 3);
  for (y = 0; y < h; y++)
    memcpy (out + (gsize) y * w * 3, j + ((gsize) (y + dy) * cw + dx) * 3,
            (gsize) w * 3);
  g_free (c);
  g_free (j);
  return out;
}

/* the numbers of a forensic case: the scene saved as a JPEG at quality
 * 90 (the camera's file), a region replaced with the same region of
 * source, optionally saved again at 90, and analysed at ela_quality.
 * Block means of 16 x 16 blocks inside the region and away from it. */
typedef struct
{
  gdouble in_mean, out_mean, in_median, out_p99;
  gdouble hit1, hit5;   /* region blocks above the 99th, 95th percentile elsewhere */
} Case;

static Case
forensic_numbers (const guint8 *source, gboolean resave, gint ela_quality)
{
  const gint    w = 512, h = 384, q = 90;
  GeglRectangle r = { 160, 112, 160, 128 };
  gfloat       *scene = make_scene (w, h, 1, 2.0);
  guint8       *s8 = to_u8 (scene, w, h);
  guint8       *f0 = jpeg_file (s8, w, h, q, FX_CHROMA_420);
  guint8       *f;
  gfloat       *fimg, *e;
  GeglBuffer   *buf;
  gdouble      *in_v, *out_v;
  gsize         n_in, n_out, i;
  Case          k = { 0, 0, 0, 0, 0, 0 };
  gdouble       p95;

  paste (f0, source, w, &r);
  f    = resave ? jpeg_file (f0, w, h, q, FX_CHROMA_420) : g_memdup2 (f0, (gsize) w * h * 3);
  fimg = from_u8 (f, w, h);
  buf  = buffer_from (fimg, w, h, WORK);
  e    = ela (buf, ela_quality, FX_CHROMA_420, 0, 0);

  in_v  = block_means (e, w, h, &r, TRUE, &n_in);
  out_v = block_means (e, w, h, &r, FALSE, &n_out);
  for (i = 0; i < n_in; i++)
    k.in_mean += in_v[i] * 255 / 20 / n_in;
  for (i = 0; i < n_out; i++)
    k.out_mean += out_v[i] * 255 / 20 / n_out;
  k.in_median = quantile (in_v, n_in, 0.5) * 255 / 20;
  k.out_p99   = quantile (out_v, n_out, 0.99) * 255 / 20;
  p95         = quantile (out_v, n_out, 0.95) * 255 / 20;
  for (i = 0; i < n_in; i++)
    {
      k.hit1 += in_v[i] * 255 / 20 > k.out_p99 ? 1.0 / n_in : 0;
      k.hit5 += in_v[i] * 255 / 20 > p95 ? 1.0 / n_in : 0;
    }
  g_free (in_v);
  g_free (out_v);
  g_free (e);
  g_object_unref (buf);
  g_free (fimg);
  g_free (f);
  g_free (f0);
  g_free (s8);
  g_free (scene);
  return k;
}

static void
forensic_report (const gchar *name, const gchar *what, gboolean ok, Case k)
{
  report (name, ok,
          "%s: mean error in the region %.2f levels, elsewhere %.2f (x %.1f); "
          "region blocks above the 99th / 95th percentile of the rest: "
          "%.0f %% / %.0f %%", what, k.in_mean, k.out_mean,
          k.in_mean / k.out_mean, 100 * k.hit1, 100 * k.hit5);
}

/* A threshold at the 99th (95th) percentile of the 16 x 16 blocks away
 * from the region flags 1 % (5 %) of them by construction: the share of
 * the region's blocks above it is the detection rate at that false alarm
 * rate. "Clearly brighter" is at least 3 times the mean error. */
static void
test_forensic (void)
{
  const gint w = 512, h = 384;
  gfloat    *scene = make_scene (w, h, 1, 2.0);
  guint8    *s8    = to_u8 (scene, w, h);
  guint8    *q70   = jpeg_file (s8, w, h, 70, FX_CHROMA_420);
  guint8    *off70 = jpeg_shifted (s8, w, h, 4, 4, 70);
  Case       k;

  /* analysed as edited (in GIMP, or saved losslessly) */
  k = forensic_numbers (s8, FALSE, 90);
  forensic_report ("forensic_never_compressed_paste",
                   "a quality 90 JPEG with a region from a never compressed "
                   "copy, ELA 90", k.hit1 >= 0.8 && k.in_mean >= 3 * k.out_mean, k);
  k = forensic_numbers (off70, FALSE, 90);
  forensic_report ("forensic_misaligned_jpeg_paste",
                   "the region from a quality 70 copy on a grid 4 pixels off",
                   k.hit5 >= 0.5 && k.in_mean >= 3 * k.out_mean, k);
  k = forensic_numbers (q70, FALSE, 90);
  forensic_report ("forensic_quality_70_paste",
                   "the region from a quality 70 copy on the same grid "
                   "(brighter on average, not block by block)",
                   k.in_mean >= 3 * k.out_mean, k);
  k = forensic_numbers (s8, FALSE, 95);
  forensic_report ("forensic_never_compressed_paste_ela_95",
                   "the never compressed paste at ELA quality 95",
                   k.hit1 >= 0.5 && k.in_mean >= 3 * k.out_mean, k);

  /* then saved again at quality 90: every block has now been saved at
   * 90, and the region only differs by having been saved once less. The
   * region is still brighter on average, in scattered blocks; at ELA
   * quality 95 it hardly differs (README, "What ELA does not show") */
  k = forensic_numbers (s8, TRUE, 90);
  forensic_report ("forensic_resaved_never_compressed_paste",
                   "the never compressed paste, saved again at 90, ELA 90",
                   k.in_mean >= 2 * k.out_mean, k);
  k = forensic_numbers (off70, TRUE, 90);
  forensic_report ("forensic_resaved_misaligned_jpeg_paste",
                   "the misaligned quality 70 paste, saved again at 90, ELA 90",
                   k.in_mean >= 2 * k.out_mean, k);
  k = forensic_numbers (q70, TRUE, 90);
  forensic_report ("forensic_resaved_quality_70_paste",
                   "the aligned quality 70 paste, saved again at 90, ELA 90",
                   k.in_mean >= 2 * k.out_mean, k);
  k = forensic_numbers (s8, TRUE, 95);
  printf ("      (the never compressed paste, saved again at 90, ELA 95: "
          "region %.2f levels, elsewhere %.2f, x %.1f: not separated)\n",
          k.in_mean, k.out_mean, k.in_mean / k.out_mean);

  g_free (off70);
  g_free (q70);
  g_free (s8);
  g_free (scene);
}

/* 3. a never compressed texture: the same error everywhere ------------- */
static gfloat *
make_texture (gint w, gint h)
{
  gfloat *p = g_new (gfloat, (gsize) w * h * 4);
  Rng     rng = { 4242 };
  gint    x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble t = 0.6 * value_noise (7, x / 5.0, y / 5.0) +
                    0.4 * value_noise (8, x / 1.7, y / 1.7);
        gfloat *q = p + ((gsize) y * w + x) * 4;

        q[0] = CLAMP (0.3 + 0.4 * t + 4.0 / 255 * rng_gauss (&rng), 0, 1);
        q[1] = CLAMP (0.35 + 0.35 * t + 4.0 / 255 * rng_gauss (&rng), 0, 1);
        q[2] = CLAMP (0.25 + 0.3 * t + 4.0 / 255 * rng_gauss (&rng), 0, 1);
        q[3] = 1;
      }
  return p;
}

static void
test_uniform (void)
{
  const gint  w = 512, h = 512;
  gfloat     *img = make_texture (w, h);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e   = ela (buf, 90, FX_CHROMA_420, 0, 0);
  gdouble     v[256], mean = 0, sd = 0, lo = G_MAXDOUBLE, hi = 0;
  gint        i;

  for (i = 0; i < 256; i++)
    {
      GeglRectangle b = { (i % 16) * 32, (i / 16) * 32, 32, 32 };

      v[i] = mean_in (e, w, h, &b, NULL, -1);
      mean += v[i] / 256;
      lo = MIN (lo, v[i]);
      hi = MAX (hi, v[i]);
    }
  for (i = 0; i < 256; i++)
    sd += (v[i] - mean) * (v[i] - mean) / 255;
  sd = sqrt (sd);
  /* 32 x 32 blocks of a stationary texture: the block means scatter only
   * by sampling (about 1/sqrt(1024) of the error's spread) */
  report ("never_compressed_texture_uniform_error",
          sd / mean < 0.1 && hi / lo < 1.5,
          "256 blocks of 32x32: mean %.2f levels, relative spread %.3f, "
          "largest / smallest %.2f", mean * 255 / 20, sd / mean, hi / lo);
  g_free (e);
  g_object_unref (buf);
  g_free (img);
}

/* 4. quality 100 ------------------------------------------------------ */
static void
test_quality_100 (void)
{
  const gint  w = 256, h = 192;
  gfloat     *img = make_scene (w, h, 3, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e444 = ela (buf, 100, FX_CHROMA_444, 0, 0);
  gfloat     *e420 = ela (buf, 100, FX_CHROMA_420, 0, 0);
  gfloat     *e90  = ela (buf, 90, FX_CHROMA_444, 0, 0);
  GeglRectangle all = { 0, 0, w, h };
  gdouble     m444 = mean_in (e444, w, h, &all, NULL, -1) * 255 / 20;
  gdouble     m420 = mean_in (e420, w, h, &all, NULL, -1) * 255 / 20;
  gdouble     m90  = mean_in (e90, w, h, &all, NULL, -1) * 255 / 20;
  gdouble     mx   = 0;
  gsize       i;

  for (i = 0; i < (gsize) w * h * 4; i += 4)
    mx = MAX (mx, MAX (MAX (e444[i], e444[i + 1]), e444[i + 2]) * 255 / 20);
  /* quality 100 quantises every DCT coefficient by 1: what is left is the
   * rounding of the integer DCT and of the color conversion, under a
   * level on average; the rounding of 64 coefficients can add up to a
   * few levels in single pixels */
  report ("quality_100_near_zero_error", m444 < 0.6 && mx <= 6.0 && m444 < m90 / 5,
          "4:4:4: mean %.3f levels, largest %.0f (quality 90: mean %.2f)",
          m444, mx, m90);
  printf ("      (quality 100 with 4:2:0: mean %.2f levels; the chroma "
          "subsampling itself loses color detail)\n", m420);
  g_free (e444);
  g_free (e420);
  g_free (e90);
  g_object_unref (buf);
  g_free (img);
}

/* 5. the grid offset -------------------------------------------------- */
static void
test_grid (void)
{
  const gint    w = 512, h = 384;
  GeglRectangle all = { 0, 0, w, h };
  gfloat       *scene = make_scene (w, h, 1, 2.0);
  guint8       *s8 = to_u8 (scene, w, h);
  guint8       *f420 = jpeg_file (s8, w, h, 90, FX_CHROMA_420);
  guint8       *f444 = jpeg_file (s8, w, h, 90, FX_CHROMA_444);
  gfloat       *i420 = from_u8 (f420, w, h), *i444 = from_u8 (f444, w, h);
  GeglBuffer   *b420 = buffer_from (i420, w, h, WORK);
  GeglBuffer   *b444 = buffer_from (i444, w, h, WORK);
  gfloat       *e0 = ela (b420, 90, FX_CHROMA_420, 0, 0);
  gfloat       *e4 = ela (b420, 90, FX_CHROMA_420, 4, 4);
  gfloat       *e8 = ela (b420, 90, FX_CHROMA_420, 8, 8);
  gfloat       *g0 = ela (b444, 90, FX_CHROMA_444, 0, 0);
  gfloat       *g8 = ela (b444, 90, FX_CHROMA_444, 8, 8);
  gdouble       m0 = mean_in (e0, w, h, &all, NULL, -1) * 255 / 20;
  gdouble       m4 = mean_in (e4, w, h, &all, NULL, -1) * 255 / 20;
  gdouble       m8 = mean_in (e8, w, h, &all, NULL, -1) * 255 / 20;

  /* on its own grid a JPEG image is near its minimum; 4 pixels off, every
   * block mixes four blocks of the file and changes much more */
  report ("grid_offset_4_changes_the_result", m4 > 2.0 * m0,
          "JPEG image, ELA on its grid: mean %.2f levels; 4 pixels off: %.2f",
          m0, m4);
  report ("grid_offset_8_same_as_0_without_subsampling",
          max_abs_diff (g0, g8, (gsize) w * h * 4) == 0.0,
          "4:4:4: the 8 x 8 grid is the same");
  report ("grid_offset_8_differs_with_4_2_0", m8 > 1.2 * m0,
          "4:2:0: the 16 x 16 chroma blocks move: mean %.2f levels, on the "
          "grid %.2f", m8, m0);

  /* a crop of 4 pixels from the left and the top: with the offset
   * (16 - 4) mod 16 = 12 the analysis is that of the uncropped image,
   * away from the partial blocks at the new edges */
  {
    gint        cw = w - 4, chh = h - 4, x, y, bad = 0;
    gfloat     *crop = g_new (gfloat, (gsize) cw * chh * 4), *ec, *ec0;
    GeglBuffer *bc;
    gdouble     d = 0, m_off = 0, m_on = 0;
    gsize       n = 0;

    for (y = 0; y < chh; y++)
      memcpy (crop + (gsize) y * cw * 4, i420 + ((gsize) (y + 4) * w + 4) * 4,
              (gsize) cw * 4 * sizeof (gfloat));
    bc  = buffer_from (crop, cw, chh, WORK);
    ec  = ela (bc, 90, FX_CHROMA_420, 12, 12);
    ec0 = ela (bc, 90, FX_CHROMA_420, 0, 0);
    for (y = 28; y < chh; y++)
      for (x = 28; x < cw; x++)
        {
          gdouble e = max_abs_diff (ec + ((gsize) y * cw + x) * 4,
                                    e0 + ((gsize) (y + 4) * w + x + 4) * 4, 3);

          d = MAX (d, e);
          bad += e != 0.0;
          m_on  += (ec[((gsize) y * cw + x) * 4] + ec[((gsize) y * cw + x) * 4 + 1] +
                    ec[((gsize) y * cw + x) * 4 + 2]) / 3;
          m_off += (ec0[((gsize) y * cw + x) * 4] + ec0[((gsize) y * cw + x) * 4 + 1] +
                    ec0[((gsize) y * cw + x) * 4 + 2]) / 3;
          n++;
        }
    report ("cropped_image_with_grid_offset_12", bad == 0 && m_off > 2.0 * m_on,
            "cropped by 4, offset 12: same as the uncropped image beyond the "
            "first 28 pixels (%d differ, largest %g); mean %.2f levels, with "
            "offset 0 %.2f", bad, d, m_on / n * 255 / 20, m_off / n * 255 / 20);
    g_free (ec);
    g_free (ec0);
    g_object_unref (bc);
    g_free (crop);
  }

  g_free (e0); g_free (e4); g_free (e8); g_free (g0); g_free (g8);
  g_object_unref (b420); g_object_unref (b444);
  g_free (i420); g_free (i444); g_free (f420); g_free (f444);
  g_free (s8); g_free (scene);
}

/* 6. determinism: again, and with other numbers of threads ------------ */
static void
test_determinism (void)
{
  const gint  w = 640, h = 480;
  gfloat     *img = make_scene (w, h, 9, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *a, *b, *c;
  gint        threads;

  g_object_get (gegl_config (), "threads", &threads, NULL);
  a = ela (buf, 80, FX_CHROMA_420, 3, 5);
  b = ela (buf, 80, FX_CHROMA_420, 3, 5);
  g_object_set (gegl_config (), "threads", threads == 1 ? 7 : 1, NULL);
  c = ela (buf, 80, FX_CHROMA_420, 3, 5);
  g_object_set (gegl_config (), "threads", threads, NULL);
  report ("deterministic",
          max_abs_diff (a, b, (gsize) w * h * 4) == 0.0 &&
          max_abs_diff (a, c, (gsize) w * h * 4) == 0.0,
          "twice, and with %d instead of %d threads", threads == 1 ? 7 : 1,
          threads);
  g_free (a); g_free (b); g_free (c);
  g_object_unref (buf);
  g_free (img);
}

/* 7. alpha ------------------------------------------------------------ */
static void
test_alpha (void)
{
  const gint  w = 97, h = 61;
  gfloat     *img = make_scene (w, h, 4, 3.0);
  gfloat     *half;
  GeglBuffer *b1, *b2;
  gfloat     *e1, *e2;
  gdouble     da = 0;
  gsize       i;

  b1 = buffer_from (img, w, h, WORK);
  e1 = ela (b1, 90, FX_CHROMA_420, 0, 0);
  half = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h; i++)
    half[4 * i + 3] = (i % w) / (gfloat) (w - 1);
  b2 = buffer_from (half, w, h, WORK);
  e2 = ela (b2, 90, FX_CHROMA_420, 0, 0);
  for (i = 0; i < (gsize) w * h; i++)
    {
      da = MAX (da, fabs (e2[4 * i + 3] - half[4 * i + 3]));
      e2[4 * i + 3] = e1[4 * i + 3];
    }
  report ("alpha_passed_through", da == 0.0 &&
          max_abs_diff (e1, e2, (gsize) w * h * 4) == 0.0,
          "alpha as it was (largest difference %g), colors as without alpha", da);
  g_free (e1); g_free (e2); g_free (half);
  g_object_unref (b1); g_object_unref (b2);
  g_free (img);
}

/* 8. NaN and infinities ----------------------------------------------- */
static void
test_nan (void)
{
  const gint  w = 40, h = 24;
  gfloat     *img = make_scene (w, h, 6, 3.0), *clean;
  GeglBuffer *b, *bc;
  gfloat     *e, *ec;
  gsize       i;
  gboolean    finite = TRUE;
  const gfloat bad[] = { NAN, INFINITY, -INFINITY, 2.0f, -1.0f, 1e30f };

  clean = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
  for (i = 0; i < (gsize) w * h * 4; i += 7)
    {
      gfloat v = bad[(i / 7) % G_N_ELEMENTS (bad)];

      img[i] = v;
      clean[i] = (i % 4 == 3) ? (v == v ? v : 0.0f)
                              : (v == v && v > 0.0f ? MIN (v, 1.0f) : 0.0f);
    }
  b  = buffer_from (img, w, h, WORK);
  bc = buffer_from (clean, w, h, WORK);
  e  = ela (b, 90, FX_CHROMA_420, 0, 0);
  ec = ela (bc, 90, FX_CHROMA_420, 0, 0);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3 && ! isfinite (e[i]))
      finite = FALSE;
  /* colors: NaN is 0, infinities and values beyond 0 to 1 are clamped,
   * as in an 8 bit file; alpha as it is, NaN as 0 */
  report ("nan_and_infinities", finite && max_abs_diff (e, ec, (gsize) w * h * 4) == 0.0,
          "finite colors, the same as with NaN as 0 and the rest clamped");
  g_free (e); g_free (ec);
  g_object_unref (b); g_object_unref (bc);
  g_free (img); g_free (clean);
}

/* 9. 8 bit, 16 bit, float, linear and gray images --------------------- */
static void
test_formats (void)
{
  const gint   w = 123, h = 77;
  gfloat      *img = make_scene (w, h, 8, 3.0);
  guint8      *u8  = to_u8 (img, w, h);
  gfloat      *exact = from_u8 (u8, w, h);
  static const gchar *formats[] = { "R'G'B'A u8", "R'G'B' u8", "R'G'B'A u16",
                                    "R'G'B'A float", "RGBA float", "RGBA u16" };
  GeglBuffer  *ref_buf = buffer_from (exact, w, h, WORK);
  gfloat      *want = ela (ref_buf, 90, FX_CHROMA_420, 0, 0);
  gint         f;

  for (f = 0; f < (gint) G_N_ELEMENTS (formats); f++)
    {
      GeglBuffer *b = buffer_from (exact, w, h, formats[f]);
      gfloat     *got = ela (b, 90, FX_CHROMA_420, 0, 0);
      gchar      *label = g_strdup_printf ("input_%s", formats[f]);
      gchar      *p;

      for (p = label; *p; p++)
        if (*p == ' ' || *p == '\'')
          *p = '_';
      report (label, max_abs_diff (got, want, (gsize) w * h * 4) == 0.0,
              "an 8 bit image in %s: the same result", formats[f]);
      g_free (label);
      g_free (got);
      g_object_unref (b);
    }

  /* 16 bit detail below 8 bit: the file would hold the rounded values */
  {
    gfloat     *fine = g_memdup2 (exact, (gsize) w * h * 4 * sizeof (gfloat));
    GeglBuffer *b;
    gfloat     *got;
    gsize       i;

    for (i = 0; i < (gsize) w * h * 4; i++)
      if (i % 4 != 3)
        fine[i] += ((gint) (((i * 2654435761u) >> 7) % 100) - 50) / 100.0f * 0.9f / 255;
    b   = buffer_from (fine, w, h, "R'G'B'A u16");
    got = ela (b, 90, FX_CHROMA_420, 0, 0);
    report ("input_16_bit_quantised_to_8_bit",
            max_abs_diff (got, want, (gsize) w * h * 4) == 0.0,
            "16 bit values within half a level of the 8 bit ones: the same "
            "result as the 8 bit image");
    g_free (got);
    g_object_unref (b);
    g_free (fine);
  }

  /* gray: R = G = B */
  {
    gfloat     *gray = g_memdup2 (exact, (gsize) w * h * 4 * sizeof (gfloat));
    GeglBuffer *b, *bg;
    gfloat     *a, *g;
    gsize       i;

    for (i = 0; i < (gsize) w * h * 4; i += 4)
      gray[i] = gray[i + 2] = gray[i + 1];
    b  = buffer_from (gray, w, h, WORK);
    bg = buffer_from (gray, w, h, "Y'A u8");
    a  = ela (b, 90, FX_CHROMA_420, 0, 0);
    g  = ela (bg, 90, FX_CHROMA_420, 0, 0);
    report ("input_gray", max_abs_diff (a, g, (gsize) w * h * 4) == 0.0,
            "Y'A u8 as the same gray R'G'B'A");
    g_free (a); g_free (g);
    g_object_unref (b); g_object_unref (bg);
    g_free (gray);
  }

  g_free (want);
  g_object_unref (ref_buf);
  g_free (exact);
  g_free (u8);
  g_free (img);
}

/* 10. auto levels, output modes, scale, quality 0 --------------------- */
static void
test_options (void)
{
  const gint  w = 200, h = 150;
  gfloat     *img = make_scene (w, h, 12, 3.0);
  GeglBuffer *buf = buffer_from (img, w, h, WORK);
  gfloat     *e, *want;
  gsize       i, n_white = 0, n = (gsize) w * h * 3;
  gdouble     top = 0;

  e = run_op (buf, OP, "auto-levels", TRUE, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3)
      {
        n_white += e[i] >= 1.0f;
        top = MAX (top, e[i]);
      }
  /* 0.6 % of the values at or above white; the error is whole levels, so
   * a few more share the level at the threshold */
  report ("auto_levels_white_at_99_4_percent",
          n_white >= 0.006 * n && n_white < 0.03 * n,
          "%.2f %% of the values are white, largest %.2f",
          100.0 * n_white / n, top);
  g_free (e);

  {
    gfloat      flat[16 * 16 * 4];
    GeglBuffer *bf;
    gboolean    black = TRUE;

    for (i = 0; i < G_N_ELEMENTS (flat); i++)
      flat[i] = i % 4 == 3 ? 1.0f : 128 / 255.0f;
    bf = buffer_from (flat, 16, 16, WORK);
    e  = run_op (bf, OP, "auto-levels", TRUE, NULL);
    for (i = 0; i < G_N_ELEMENTS (flat); i++)
      if (i % 4 != 3 && e[i] != 0.0f)
        black = FALSE;
    report ("auto_levels_no_error_stays_black", black, "a flat gray image");
    g_free (e);
    g_object_unref (bf);
  }

  for (i = 1; i <= 2; i++)
    {
      gdouble d;

      e    = run_op (buf, OP, "mode", (gint) i, "quality", 75, NULL);
      want = reference (img, w, h, 75, FX_CHROMA_420, 0, 0, 20.0, (gint) i);
      d    = max_abs_diff (e, want, (gsize) w * h * 4);
      report (i == 1 ? "output_luminance" : "output_largest_channel", d < 1e-6,
              "against the reference: largest difference %g", d);
      g_free (e);
      g_free (want);
    }

  {
    gboolean black = TRUE;

    e = run_op (buf, OP, "scale", 0.0, NULL);
    for (i = 0; i < (gsize) w * h * 4; i++)
      if (i % 4 != 3 && e[i] != 0.0f)
        black = FALSE;
    report ("scale_0_black", black, NULL);
    g_free (e);
  }

  e    = run_op (buf, OP, "quality", 0, "scale", 1.0, NULL);
  want = reference (img, w, h, 0, FX_CHROMA_420, 0, 0, 1.0, 0);
  report ("quality_0", max_abs_diff (e, want, (gsize) w * h * 4) == 0.0,
          "mean error %.1f levels", mean_in (e, w, h, &(GeglRectangle) { 0, 0, w, h },
                                             NULL, -1) * 255);
  g_free (e);
  g_free (want);

  g_object_unref (buf);
  g_free (img);
}

/* 11. GEGL's own JPEG save and load (4:4:4, float IDCT) ---------------- */
static void
test_gegl_jpeg (void)
{
  const gint  w = 320, h = 240;
  gfloat     *img = make_scene (w, h, 13, 3.0);
  guint8     *u8 = to_u8 (img, w, h);
  gfloat     *exact = from_u8 (u8, w, h);
  GeglBuffer *buf = buffer_from (exact, w, h, WORK);
  gchar      *dir = g_dir_make_tmp ("forensics-jpg-XXXXXX", NULL);
  gchar      *path = g_build_filename (dir, "copy.jpg", NULL);
  GeglNode   *g = gegl_node_new ();
  GeglNode   *src = gegl_node_new_child (g, "operation", "gegl:buffer-source",
                                         "buffer", buf, NULL);
  GeglNode   *save = gegl_node_new_child (g, "operation", "gegl:jpg-save",
                                          "path", path, "quality", 85, NULL);
  GeglNode   *load, *g2;
  gfloat     *copy = g_new (gfloat, (gsize) w * h * 4), *e;
  gdouble     mean = 0, mx = 0;
  gsize       i;

  gegl_node_link (src, save);
  gegl_node_process (save);
  g_object_unref (g);
  g2   = gegl_node_new ();
  load = gegl_node_new_child (g2, "operation", "gegl:jpg-load", "path", path, NULL);
  gegl_node_blit (load, 1.0, &(GeglRectangle) { 0, 0, w, h }, babl_format (WORK),
                  copy, GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (g2);
  e = run_op (buf, OP, "quality", 85, "chroma", FX_CHROMA_444, "scale", 1.0, NULL);
  for (i = 0; i < (gsize) w * h * 4; i++)
    if (i % 4 != 3)
      {
        gdouble ela_gegl = fabs (exact[i] - copy[i]) * 255;
        gdouble d        = fabs (ela_gegl - e[i] * 255);

        mean += d / (w * h * 3);
        mx = MAX (mx, d);
      }
  /* jpg-load decodes with the float IDCT: a level or a few here and
   * there, 0.1 levels on average at most */
  report ("like_gegl_jpg_save_and_load", mean < 0.1 && mx <= 4.0,
          "quality 85, 4:4:4: the error differs by %.3f levels on average, "
          "at most %.0f", mean, mx);
  g_unlink (path);
  g_rmdir (dir);
  g_free (path);
  g_free (dir);
  g_free (e);
  g_free (copy);
  g_object_unref (buf);
  g_free (exact);
  g_free (u8);
  g_free (img);
}

/* 12. a changed pixel changes nothing outside the invalidated region -- */
static void
test_invalidated (void)
{
  const gint  w = 160, h = 128;
  gfloat     *img = make_scene (w, h, 14, 3.0);
  gint        c, bad = 0;

  for (c = 0; c < 3; c++)
    {
      GeglBuffer   *b1 = buffer_from (img, w, h, WORK), *b2;
      gfloat       *changed = g_memdup2 (img, (gsize) w * h * 4 * sizeof (gfloat));
      GeglRectangle px = { 77, 50, 1, 1 }, inv;
      GeglNode     *graph = gegl_node_new ();
      GeglNode     *node  = gegl_node_new_child (graph, "operation", OP,
                                                 "chroma", c, "grid-x", 3, NULL);
      GeglNode     *src   = gegl_node_new_child (graph, "operation",
                                                 "gegl:buffer-source", "buffer",
                                                 b1, NULL);
      gfloat       *e1, *e2;
      gint          x, y;

      gegl_node_link (src, node);
      gegl_node_blit (node, 1.0, &(GeglRectangle) { 0, 0, w, h }, NULL, NULL, 0,
                      GEGL_BLIT_DEFAULT);
      inv = gegl_operation_get_invalidated_by_change (gegl_node_get_gegl_operation (node),
                                                      "input", &px);
      g_object_unref (graph);
      changed[(50 * w + 77) * 4] = 1.0f - changed[(50 * w + 77) * 4];
      b2 = buffer_from (changed, w, h, WORK);
      e1 = run_op (b1, OP, "chroma", c, "grid-x", 3, NULL);
      e2 = run_op (b2, OP, "chroma", c, "grid-x", 3, NULL);
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          if (max_abs_diff (e1 + ((gsize) y * w + x) * 4,
                            e2 + ((gsize) y * w + x) * 4, 4) != 0.0 &&
              ! (x >= inv.x && x < inv.x + inv.width &&
                 y >= inv.y && y < inv.y + inv.height))
            bad++;
      g_free (e1); g_free (e2); g_free (changed);
      g_object_unref (b1); g_object_unref (b2);
    }
  report ("changes_within_invalidated_region", bad == 0,
          "%d changed pixels outside it", bad);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <error-level.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;

  test_reference ();
  test_pieces ();
  test_forensic ();
  test_uniform ();
  test_quality_100 ();
  test_grid ();
  test_determinism ();
  test_alpha ();
  test_nan ();
  test_formats ();
  test_options ();
  test_gegl_jpeg ();
  test_invalidated ();

  return check_end ();
}
