/*
 * JPEG round trips for the forensics operations (error-level, jpeg-ghost)
 *
 * forensics-jpeg.h
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Included (not linked) by each operation that needs it, so that every
 * module stays one file for GEGL and nothing is exported twice.
 *
 * An image is compressed as a JPEG file with libjpeg (in memory, never on
 * disk) and decoded again, the way a program that saves the image as JPEG
 * at quality Q and opens it again sees it: 8 bit R'G'B' values, the IJG
 * quantisation tables scaled for Q (jpeg_set_quality, baseline), the
 * integer DCT, and the chosen chroma subsampling; decoding with libjpeg's
 * defaults (integer IDCT, "fancy" chroma upsampling).
 *
 * The JPEG block grid: the image is placed on a "canvas" whose blocks
 * start where the user says the original file's blocks were (the grid
 * offset, 0 to 15 pixels from the left and top edges of the layer). With
 * an offset, the part of the image left of or above the first grid line
 * lies in a partial block; the canvas fills that block with copies of the
 * edge pixels (the result there is discarded). The right and bottom edges
 * end where the image ends, as they would in a file of the image.
 *
 * Pieces: JPEG blocks are coded independently (only the DC coefficients
 * are predicted, losslessly), so a piece of the canvas that starts and
 * ends on MCU lines decodes to the same pixels as the whole canvas, with
 * one exception: fancy chroma upsampling looks at the neighbouring chroma
 * samples, which a piece does not have at its edges. With subsampled
 * chroma a piece therefore gets one more MCU on each side (a margin),
 * which is discarded. The piece's result is then exactly that of the
 * whole image (checked by tests/check-error-level.c against a round trip
 * of the whole image).
 */

#ifndef FORENSICS_JPEG_H
#define FORENSICS_JPEG_H

#include <gegl.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

/* chroma subsampling, in the order of the enum properties */
enum
{
  FX_CHROMA_420 = 0,  /* 2x2: most cameras and programs */
  FX_CHROMA_422 = 1,  /* 2x1 */
  FX_CHROMA_444 = 2   /* none */
};

/* the canvas period: the largest MCU (16 x 16 for 4:2:0) */
#define FX_PERIOD 16

typedef struct
{
  gint x0, y0;      /* the canvas's top left corner, in image coordinates */
  gint w, h;        /* its size */
  gint mcu_w, mcu_h;
  gint margin_x, margin_y;
  GeglRectangle bbox;  /* the image */
} FxCanvas;

static inline void
fx_canvas_init (FxCanvas            *c,
                const GeglRectangle *bbox,
                gint                 offset_x,
                gint                 offset_y,
                gint                 chroma)
{
  gint pad_x = (FX_PERIOD - (offset_x % FX_PERIOD)) % FX_PERIOD;
  gint pad_y = (FX_PERIOD - (offset_y % FX_PERIOD)) % FX_PERIOD;

  c->bbox     = *bbox;
  c->x0       = bbox->x - pad_x;
  c->y0       = bbox->y - pad_y;
  c->w        = bbox->width + pad_x;
  c->h        = bbox->height + pad_y;
  c->mcu_w    = chroma == FX_CHROMA_444 ? 8 : 16;
  c->mcu_h    = chroma == FX_CHROMA_420 ? 16 : 8;
  c->margin_x = chroma == FX_CHROMA_444 ? 0 : c->mcu_w;
  c->margin_y = chroma == FX_CHROMA_420 ? c->mcu_h : 0;
}

static inline gint
fx_floor_to (gint v, gint m)
{
  return (v >= 0 ? v / m : -((-v + m - 1) / m)) * m;
}

/* the piece of the canvas (in image coordinates) whose round trip gives
 * the exact pixels of need (which lies within the image) */
static inline GeglRectangle
fx_canvas_piece (const FxCanvas      *c,
                 const GeglRectangle *need)
{
  GeglRectangle r;
  gint x1 = need->x - c->x0, y1 = need->y - c->y0;
  gint x2 = x1 + need->width, y2 = y1 + need->height;

  x1 = fx_floor_to (x1, c->mcu_w) - c->margin_x;
  y1 = fx_floor_to (y1, c->mcu_h) - c->margin_y;
  x2 = fx_floor_to (x2 + c->mcu_w - 1, c->mcu_w) + c->margin_x;
  y2 = fx_floor_to (y2 + c->mcu_h - 1, c->mcu_h) + c->margin_y;
  x1 = MAX (x1, 0);
  y1 = MAX (y1, 0);
  x2 = MIN (x2, c->w);
  y2 = MIN (y2, c->h);

  r.x = c->x0 + x1;
  r.y = c->y0 + y1;
  r.width  = MAX (x2 - x1, 0);
  r.height = MAX (y2 - y1, 0);
  return r;
}

/* the part of the image a piece reads (the piece within the image) */
static inline GeglRectangle
fx_canvas_source (const FxCanvas      *c,
                  const GeglRectangle *piece)
{
  GeglRectangle r;

  gegl_rectangle_intersect (&r, piece, &c->bbox);
  return r;
}

/* float to 8 bit, as babl does it (x 255, rounded, clamped), with NaN
 * as 0 and infinities clamped */
static inline guint8
fx_to_u8 (gfloat v)
{
  if (! (v > 0.0f))          /* also NaN */
    return 0;
  if (v >= 1.0f)
    return 255;
  return (guint8) (v * 255.0f + 0.5f);
}

/* reads the piece (image coordinates) from input as 8 bit R'G'B' (in
 * format, an R'G'B'A float format), with the parts outside the image
 * filled with copies of the edge pixels. rgb holds piece->width x
 * piece->height x 3 bytes; alpha (may be NULL) the alpha of the pixels
 * as floats. */
static inline void
fx_read_piece (GeglBuffer          *input,
               const Babl          *format,
               const FxCanvas      *c,
               const GeglRectangle *piece,
               guint8              *rgb)
{
  GeglRectangle src = fx_canvas_source (c, piece);
  gfloat       *buf;
  gint          x, y;

  if (src.width <= 0 || src.height <= 0)
    return;

  buf = g_new (gfloat, (gsize) src.width * src.height * 4);
  gegl_buffer_get (input, &src, 1.0, format, buf,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_CLAMP);

  for (y = 0; y < piece->height; y++)
    {
      gint          sy  = CLAMP (piece->y + y, src.y, src.y + src.height - 1) - src.y;
      const gfloat *row = buf + (gsize) sy * src.width * 4;
      guint8       *out = rgb + (gsize) y * piece->width * 3;

      for (x = 0; x < piece->width; x++)
        {
          gint          sx = CLAMP (piece->x + x, src.x, src.x + src.width - 1) - src.x;
          const gfloat *p  = row + (gsize) sx * 4;

          out[3 * x + 0] = fx_to_u8 (p[0]);
          out[3 * x + 1] = fx_to_u8 (p[1]);
          out[3 * x + 2] = fx_to_u8 (p[2]);
        }
    }
  g_free (buf);
}

/* libjpeg errors: back to the caller instead of exit () */
typedef struct
{
  struct jpeg_error_mgr pub;
  jmp_buf               jump;
} FxJpegError;

static inline void
fx_jpeg_error_exit (j_common_ptr cinfo)
{
  FxJpegError *err = (FxJpegError *) cinfo->err;

  longjmp (err->jump, 1);
}

static inline void
fx_jpeg_no_message (j_common_ptr cinfo)
{
  (void) cinfo;
}

/* compresses the w x h R'G'B' pixels in rgb as a JPEG at quality (0 to
 * 100; libjpeg takes 0 as 1) with the chroma subsampling, and decodes it
 * into out (w x h x 3). Returns FALSE (out undefined) if libjpeg failed,
 * which should not happen. */
static inline gboolean
fx_jpeg_round_trip (const guint8 *rgb,
                    gint          w,
                    gint          h,
                    gint          quality,
                    gint          chroma,
                    guint8       *out)
{
  struct jpeg_compress_struct   cc;
  struct jpeg_decompress_struct dc;
  FxJpegError                   cerr, derr;
  unsigned char                *mem     = NULL;  /* (its address is taken) */
  unsigned long                 size    = 0;
  volatile gboolean             ok      = FALSE;
  volatile gboolean             dc_made = FALSE;

  if (w <= 0 || h <= 0)
    return TRUE;

  memset (&cc, 0, sizeof cc);
  memset (&dc, 0, sizeof dc);
  cc.err = jpeg_std_error (&cerr.pub);
  cerr.pub.error_exit     = fx_jpeg_error_exit;
  cerr.pub.output_message = fx_jpeg_no_message;

  if (setjmp (cerr.jump))
    {
      jpeg_destroy_compress (&cc);
      free (mem);
      return FALSE;
    }

  jpeg_create_compress (&cc);
  {
    jpeg_mem_dest (&cc, &mem, &size);
    cc.image_width      = (JDIMENSION) w;
    cc.image_height     = (JDIMENSION) h;
    cc.input_components = 3;
    cc.in_color_space   = JCS_RGB;
    jpeg_set_defaults (&cc);
    jpeg_set_quality (&cc, CLAMP (quality, 0, 100), TRUE);
    cc.dct_method = JDCT_ISLOW;
    cc.comp_info[0].h_samp_factor = chroma == FX_CHROMA_444 ? 1 : 2;
    cc.comp_info[0].v_samp_factor = chroma == FX_CHROMA_420 ? 2 : 1;
    cc.comp_info[1].h_samp_factor = cc.comp_info[1].v_samp_factor = 1;
    cc.comp_info[2].h_samp_factor = cc.comp_info[2].v_samp_factor = 1;
    jpeg_start_compress (&cc, TRUE);
    while (cc.next_scanline < cc.image_height)
      {
        JSAMPROW row = (JSAMPROW) (rgb + (gsize) cc.next_scanline * w * 3);

        jpeg_write_scanlines (&cc, &row, 1);
      }
    jpeg_finish_compress (&cc);
  }
  jpeg_destroy_compress (&cc);

  dc.err = jpeg_std_error (&derr.pub);
  derr.pub.error_exit     = fx_jpeg_error_exit;
  derr.pub.output_message = fx_jpeg_no_message;
  if (setjmp (derr.jump))
    {
      if (dc_made)
        jpeg_destroy_decompress (&dc);
      free (mem);
      return FALSE;
    }
  jpeg_create_decompress (&dc);
  dc_made = TRUE;
  jpeg_mem_src (&dc, mem, size);
  jpeg_read_header (&dc, TRUE);
  dc.out_color_space     = JCS_RGB;
  dc.dct_method          = JDCT_ISLOW;
  dc.do_fancy_upsampling = TRUE;
  jpeg_start_decompress (&dc);
  if (dc.output_width == (JDIMENSION) w && dc.output_height == (JDIMENSION) h &&
      dc.output_components == 3)
    {
      while (dc.output_scanline < dc.output_height)
        {
          JSAMPROW row = (JSAMPROW) (out + (gsize) dc.output_scanline * w * 3);

          jpeg_read_scanlines (&dc, &row, 1);
        }
      jpeg_finish_decompress (&dc);
      ok = TRUE;
    }
  else
    {
      jpeg_abort_decompress (&dc);
    }
  jpeg_destroy_decompress (&dc);
  free (mem);
  return ok;
}

/* the rectangles of the pieces: rows of MCUs of about rows_per_piece
 * rows, covering the rows of roi. Returns the number, fills starts (the
 * first row of each, n + 1 entries) */
static inline gint
fx_split_rows (const FxCanvas      *c,
               const GeglRectangle *roi,
               gint                 n_wanted,
               gint               **starts)
{
  gint top    = roi->y;
  gint bottom = roi->y + roi->height;
  gint rows, n, i;

  n_wanted = MAX (n_wanted, 1);
  rows = (roi->height + n_wanted - 1) / n_wanted;
  rows = MAX (rows, 64);
  rows = fx_floor_to (rows + c->mcu_h - 1, c->mcu_h);
  n    = 0;
  *starts = g_new (gint, roi->height / c->mcu_h + 3);
  (*starts)[n++] = top;
  {
    /* cut on MCU lines of the canvas */
    gint y = c->y0 + fx_floor_to (top - c->y0, c->mcu_h) + rows;

    while (y < bottom)
      {
        (*starts)[n++] = y;
        y += rows;
      }
  }
  (*starts)[n] = bottom;
  for (i = 0; i < n; i++)
    g_assert ((*starts)[i] < (*starts)[i + 1]);
  return n;
}

#endif
