/*
 * Checks of forensics:bit-plane
 *
 * check-bit-plane.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   check-bit-plane <bit-plane.so>
 *
 * Every bit of every channel exactly, against the 8 bit values computed
 * here (OpenCV's gray in fixed point, the RGB norm); the rounding of float
 * values to 8 bit; a ramp's highest plane; the standard checks.
 */

#include "check-common.h"

#define OP "forensics:bit-plane"

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
value_of (const gfloat *p, gint channel)
{
  gint r = u8 (p[0]), g = u8 (p[1]), b = u8 (p[2]);

  switch (channel)
    {
    case 1: return r;
    case 2: return g;
    case 3: return b;
    case 4: return (gint) (sqrt ((r * r + g * g + b * b) / 3.0) + 0.5);
    default: return (r * 4899 + g * 9617 + b * 1868 + 8192) >> 14;
    }
}

static void
test_exact (void)
{
  const gint  w = 61, h = 47;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
  Rng         rng = { 9 };
  GeglBuffer *buf;
  gint        channel, bit, bad = 0, cases = 0;
  gsize       i;

  for (i = 0; i < (gsize) w * h * 4; i++)
    img[i] = i % 4 == 3 ? 1.0f : (rng_next (&rng) % 256) / 255.0f;
  buf = buffer_from (img, w, h, WORK);
  for (channel = 0; channel < 5; channel++)
    for (bit = 0; bit < 8; bit++)
      {
        gfloat *out = run_op (buf, OP, "channel", channel, "bit", bit, NULL);

        for (i = 0; i < (gsize) w * h; i++)
          {
            gfloat want = (value_of (img + 4 * i, channel) >> bit) & 1 ? 1.0f : 0.0f;

            if (out[4 * i] != want || out[4 * i + 1] != want || out[4 * i + 2] != want)
              bad++;
          }
        cases++;
        g_free (out);
      }
  report ("every_bit_of_every_channel_exact", bad == 0,
          "%d channels x bits on %d random pixels: %d differ", cases, w * h, bad);
  g_object_unref (buf);
  g_free (img);
}

static void
test_rounding_and_ramp (void)
{
  const gint  w = 256, h = 2;
  gfloat     *img = g_new (gfloat, (gsize) w * h * 4);
  GeglBuffer *buf;
  gfloat     *out, *out0;
  gint        x, bad = 0;

  for (x = 0; x < w * h; x++)
    {
      gfloat v = (x % w) / 255.0f;

      /* a little off the 8 bit values: rounded to them */
      img[4 * x] = img[4 * x + 1] = img[4 * x + 2] = v + ((x / w) ? 0.4f : -0.4f) / 255.0f;
      img[4 * x + 3] = 1;
    }
  buf = buffer_from (img, w, h, WORK);
  out = run_op (buf, OP, "channel", 1, "bit", 7, NULL);
  out0 = run_op (buf, OP, "channel", 1, "bit", 0, NULL);
  for (x = 0; x < w * h; x++)
    {
      gint v = x % w;

      if (out[4 * x] != (v >= 128 ? 1.0f : 0.0f) || out0[4 * x] != (v & 1 ? 1.0f : 0.0f))
        bad++;
    }
  report ("ramp_planes_and_rounding", bad == 0,
          "plane 7 of a ramp 0 to 255 white from 128, plane 0 every other level, values "
          "0.4 levels off rounded: %d differ", bad);
  g_free (out);
  g_free (out0);
  g_object_unref (buf);
  g_free (img);
}

gint
main (gint    argc,
      gchar **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <bit-plane.so>\n", argv[0]);
      return 2;
    }
  if (! check_start (&argc, &argv, OP, NULL))
    return 2;
  test_exact ();
  test_rounding_and_ramp ();
  check_standard ("luma_bit_2", OP, "bit", 2);
  check_standard ("norm", OP, "channel", 4);
  return check_end ();
}
