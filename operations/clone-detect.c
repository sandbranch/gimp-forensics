/*
 * Clone (copy-move) detection, a GEGL operation
 *
 * clone-detect.c
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
 * Finds regions copied to another place of the same image (the clone
 * tool, or copy and paste within the picture), after J. Fridrich,
 * D. Soukal and J. Lukas, "Detection of Copy-Move Forgery in Digital
 * Images", Digital Forensic Research Workshop 2003: every b x b block
 * (at every position) gets the quantised low frequency coefficients of
 * its DCT; the blocks are sorted lexicographically by them, so that
 * equal blocks lie next to each other; each pair of equal blocks at
 * least b apart votes for its shift vector; shifts with enough votes are
 * copies. Blocks with less fine detail than a threshold (flat sky,
 * walls) are left out (Forensically's "minimal detail" does the same),
 * and a pair of equal blocks counts only when their pixels are alike
 * (their mean absolute difference at most the tolerance and a quarter
 * of their detail) and neither also matches itself moved a few pixels
 * (a straight edge or stripes, which match along themselves). Written
 * for this operation.
 *
 * The analysis needs the whole image; large images are first reduced
 * (box average, by a whole factor) so that their longer side is at most
 * "analysis size" pixels. Shown: the image darkened, with the blocks of
 * each copy and its original tinted in a color of their shift (lines
 * join the middle of the original and of the copy), or a mask (white on
 * black). Alpha is passed through.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (forensics_clone_mode)
  enum_value (FORENSICS_CLONE_OVERLAY, "overlay", N_("Over the image"))
  enum_value (FORENSICS_CLONE_MASK,    "mask",    N_("Mask"))
enum_end (ForensicsCloneMode)

property_int (block_size, _("Block size"), 16)
  description (_("The size of the blocks that are compared (Fridrich "
                 "uses 16)"))
  value_range (8, 32)

property_double (tolerance, _("Tolerance"), 2.0)
  description (_("How different two blocks may be and still count as "
                 "the same: their mean difference in levels of 8 bit (and "
                 "the step of the quantisation of their DCT). A copy also "
                 "differs by at most a quarter of the blocks' detail."))
  value_range (0.25, 20.0)
  ui_range (0.5, 10.0)

property_double (min_detail, _("Minimal detail"), 3.0)
  description (_("Blocks with less fine detail than this (the RMS of the "
                 "block beyond its mean and its softest gradients, in "
                 "levels) are not compared: flat or smooth areas would all "
                 "match"))
  value_range (0.0, 50.0)
  ui_range (0.0, 20.0)

property_int (min_matches, _("Minimal matches"), 40)
  description (_("How many pairs of blocks must share a shift before it "
                 "counts as a copy"))
  value_range (1, 100000)
  ui_range (5, 500)
  ui_gamma (2.0)

property_int (min_distance, _("Minimal distance"), 0)
  description (_("How far a copy must be from its original, in pixels of "
                 "the analysis (0: the block size)"))
  value_range (0, 10000)
  ui_range (0, 200)

property_int (analysis_size, _("Analysis size"), 1536)
  description (_("Larger images are reduced for the analysis so that "
                 "their longer side has at most this many pixels; smaller "
                 "copies are then missed, larger sizes take longer"))
  value_range (128, 8192)
  ui_range (512, 4096)

property_enum (mode, _("Show"), ForensicsCloneMode, forensics_clone_mode,
               FORENSICS_CLONE_OVERLAY)
  description (_("The copies over the darkened image, or a mask"))

property_boolean (lines, _("Lines"), TRUE)
  description (_("Join the middle of each original and of its copy with a "
                 "line (over the image)"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     clone_detect
#define GEGL_OP_C_SOURCE clone-detect.c

#include "gegl-op.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the low frequencies kept: u, v < N_FREQ */
#define N_FREQ 4
#define N_FEAT (N_FREQ * N_FREQ)
/* a block is compared with this many of its neighbours in the sorted
 * list (equal features only) */
#define MAX_NEIGHBOURS 8

static const Babl *
work_format (GeglOperation *operation)
{
  return babl_format_with_space ("R'G'B'A float",
                                 gegl_operation_get_source_space (operation,
                                                                  "input"));
}

static void
prepare (GeglOperation *operation)
{
  const Babl *format = work_format (operation);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation,
                                                                    "input");
  GeglRectangle        r  = { 0, 0, 0, 0 };

  if (in)
    r = *in;
  return r;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle r = get_bounding_box (operation);

  (void) input_pad;
  if (gegl_rectangle_is_infinite_plane (&r))
    return *roi;
  return r;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  (void) input_pad;
  (void) roi;
  return get_bounding_box (operation);
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  GeglRectangle r = get_bounding_box (operation);

  if (gegl_rectangle_is_infinite_plane (&r))
    return *roi;
  return r;
}

typedef struct
{
  gint16 f[N_FEAT];
  gint32 x, y;
  gfloat detail;   /* the RMS of the block around its mean */
} Block;

static gint
cmp_block (gconstpointer a, gconstpointer b)
{
  const Block *p = a, *q = b;
  gint         i;

  for (i = 0; i < N_FEAT; i++)
    if (p->f[i] != q->f[i])
      return p->f[i] < q->f[i] ? -1 : 1;
  /* (then by place: the same order on every run) */
  if (p->y != q->y)
    return p->y < q->y ? -1 : 1;
  return (p->x > q->x) - (p->x < q->x);
}

typedef struct
{
  const gfloat *luma;       /* w x h, 0 to 255 */
  gint          w, h, b;
  gdouble       basis[N_FREQ][32];
  gdouble       step[N_FEAT];
  gdouble       min_detail2;
  gdouble      *rows;       /* per pixel: N_FREQ coefficients of the b pixels right of it */
  Block        *blocks;     /* one per block position, or empty */
  guint8       *kept;
} Features;

/* the 1D DCT (the N_FREQ lowest) of the b pixels at each x of rows */
static void
row_dcts (gsize first, gsize n, gpointer data)
{
  Features *f = data;
  gsize     y;

  for (y = first; y < first + n; y++)
    {
      const gfloat *l = f->luma + y * f->w;
      gint          x, u, k;

      for (x = 0; x + f->b <= f->w; x++)
        for (u = 0; u < N_FREQ; u++)
          {
            gdouble s = 0;

            for (k = 0; k < f->b; k++)
              s += f->basis[u][k] * l[x + k];
            f->rows[(y * f->w + x) * N_FREQ + u] = s;
          }
    }
}

/* the features of the blocks whose top row is y */
static void
block_features (gsize first, gsize n, gpointer data)
{
  Features *f = data;
  gsize     y;
  gint      bw = f->w - f->b + 1;

  for (y = first; y < first + n; y++)
    {
      gint x;

      for (x = 0; x < bw; x++)
        {
          Block  *blk = f->blocks + y * bw + x;
          gdouble c[N_FEAT], ac = 0, sum = 0, sum2 = 0;
          gint    u, v, k;

          for (v = 0; v < N_FREQ; v++)
            for (u = 0; u < N_FREQ; u++)
              {
                gdouble s = 0;

                for (k = 0; k < f->b; k++)
                  s += f->basis[v][k] * f->rows[((y + k) * f->w + x) * N_FREQ + u];
                c[v * N_FREQ + u] = s;
              }
          /* the detail: the RMS of the block beyond its mean and its
           * lowest frequencies (a gradient, a soft cloud): the energy of
           * the orthonormal DCT outside u, v < 2 */
          for (k = 0; k < f->b; k++)
            {
              const gfloat *l = f->luma + (y + k) * f->w + x;
              gint          i;

              for (i = 0; i < f->b; i++)
                {
                  sum  += l[i];
                  sum2 += (gdouble) l[i] * l[i];
                }
            }
          ac = sum2 - c[0] * c[0] - c[1] * c[1] - c[N_FREQ] * c[N_FREQ] -
               c[N_FREQ + 1] * c[N_FREQ + 1];
          ac = MAX (ac, 0.0) / (f->b * f->b);
          (void) sum;
          f->kept[y * bw + x] = ac >= f->min_detail2 && ac > 0;
          blk->detail = (gfloat) sqrt (ac);
          for (k = 0; k < N_FEAT; k++)
            blk->f[k] = (gint16) CLAMP (floor (c[k] / f->step[k] + 0.5), -32767, 32767);
          blk->x = x;
          blk->y = (gint32) y;
        }
    }
}

typedef struct
{
  gint32  dx, dy;
  gint    count;
  gint    id;       /* 1 + its place among the kept shifts, or 0 */
  gdouble sx, sy;   /* sums of the originals' corners, for the lines */
} Shift;

static inline gint64
shift_key (gint dx, gint dy)
{
  return ((gint64) dx << 32) ^ (guint32) dy;
}

/* the pairs of blocks with equal features, at least min_dist apart,
 * whose pixels really are alike: their mean absolute difference at most
 * the tolerance and at most a quarter of their detail (two unrelated
 * blocks of a texture differ about as much as their detail, a copy by
 * its noise or compression only). Their shifts normalised (dx > 0, or
 * dx = 0 and dy > 0); calls fn for each */
typedef void (*PairFunc) (const Block *a, const Block *b, gint dx, gint dy,
                          gpointer data);

typedef struct
{
  const gfloat *luma;
  gint          w, h, b;
  gint          min_dist2;
  gdouble       tolerance;
} Pairs;

/* the mean absolute difference of the blocks at (ax, ay) and (bx, by)
 * is at most limit */
static gboolean
within (const Pairs *p, gint ax, gint ay, gint bx, gint by, gdouble limit)
{
  gdouble sum = 0, stop = limit * p->b * p->b;
  gint    i, j;

  for (j = 0; j < p->b; j++)
    {
      const gfloat *la = p->luma + (gsize) (ay + j) * p->w + ax;
      const gfloat *lb = p->luma + (gsize) (by + j) * p->w + bx;

      for (i = 0; i < p->b; i++)
        sum += fabsf (la[i] - lb[i]);
      if (sum > stop)
        return FALSE;
    }
  return TRUE;
}

/* a block on a straight edge or in stripes matches itself moved along
 * them: it matches any block further along too, a copy or not. Such a
 * block (it matches itself moved 3 pixels across, down or diagonally) is
 * left out */
static gboolean
self_similar (const Pairs *p, const Block *a, gdouble limit)
{
  static const gint moves[4][2] = { { 3, 0 }, { 0, 3 }, { 2, 2 }, { 2, -2 } };
  gint              k;

  for (k = 0; k < 4; k++)
    {
      gint x = a->x + moves[k][0], y = a->y + moves[k][1];

      if (x >= 0 && y >= 0 && x + p->b <= p->w && y + p->b <= p->h &&
          within (p, a->x, a->y, x, y, limit))
        return TRUE;
    }
  return FALSE;
}

static gboolean
alike (const Pairs *p, const Block *a, const Block *b)
{
  gdouble limit = MIN (p->tolerance, 0.25 * MIN (a->detail, b->detail));

  return within (p, a->x, a->y, b->x, b->y, limit) &&
         ! self_similar (p, a, limit) && ! self_similar (p, b, limit);
}

static void
for_each_pair (const Block *blocks, gsize n, const Pairs *p, PairFunc fn,
               gpointer data)
{
  gsize i, j;

  for (i = 0; i < n; i++)
    for (j = i + 1; j < n && j <= i + MAX_NEIGHBOURS; j++)
      {
        const Block *a = blocks + i, *b = blocks + j;
        gint         dx, dy;

        if (memcmp (a->f, b->f, sizeof a->f) != 0)
          break;
        dx = b->x - a->x;
        dy = b->y - a->y;
        if (dx * dx + dy * dy < p->min_dist2 || ! alike (p, a, b))
          continue;
        if (dx < 0 || (dx == 0 && dy < 0))
          {
            const Block *t = a;

            a = b;
            b = t;
            dx = -dx;
            dy = -dy;
          }
        fn (a, b, dx, dy, data);
      }
}

static void
count_pair (const Block *a, const Block *b, gint dx, gint dy, gpointer data)
{
  GHashTable *shifts = data;
  gint64      key    = shift_key (dx, dy);
  Shift      *s      = g_hash_table_lookup (shifts, &key);

  (void) b;
  if (! s)
    {
      gint64 *k = g_new (gint64, 1);

      *k = key;
      s = g_new0 (Shift, 1);
      s->dx = dx;
      s->dy = dy;
      g_hash_table_insert (shifts, k, s);
    }
  s->count++;
  s->sx += a->x;
  s->sy += a->y;
}

typedef struct
{
  GHashTable *shifts;
  gint        b, w, h;
  gint32     *owner;    /* per analysis pixel: the id of a kept shift, or 0 */
} Marking;

static void
mark_pair (const Block *a, const Block *b, gint dx, gint dy, gpointer data)
{
  Marking *m   = data;
  gint64   key = shift_key (dx, dy);
  Shift   *s   = g_hash_table_lookup (m->shifts, &key);
  gint     x, y, id;

  if (! s || ! s->id)
    return;
  id = s->id;
  for (y = 0; y < m->b; y++)
    for (x = 0; x < m->b; x++)
      {
        m->owner[(gsize) (a->y + y) * m->w + a->x + x] = id;
        m->owner[(gsize) (b->y + y) * m->w + b->x + x] = id;
      }
}

/* a color for a shift: its direction as a hue, full saturation */
static void
shift_color (gint dx, gint dy, gfloat *rgb)
{
  gdouble h  = atan2 (-dy, dx) / (2 * G_PI);
  gdouble h6, f;
  gint    i;

  h  = h < 0 ? h + 1 : h;
  h6 = h * 6;
  i  = (gint) h6 % 6;
  f  = h6 - floor (h6);
  switch (i)
    {
    case 0:  rgb[0] = 1;     rgb[1] = f;     rgb[2] = 0;     break;
    case 1:  rgb[0] = 1 - f; rgb[1] = 1;     rgb[2] = 0;     break;
    case 2:  rgb[0] = 0;     rgb[1] = 1;     rgb[2] = f;     break;
    case 3:  rgb[0] = 0;     rgb[1] = 1 - f; rgb[2] = 1;     break;
    case 4:  rgb[0] = f;     rgb[1] = 0;     rgb[2] = 1;     break;
    default: rgb[0] = 1;     rgb[1] = 0;     rgb[2] = 1 - f; break;
    }
}

static gint
cmp_shift_count (gconstpointer a, gconstpointer b)
{
  const Shift *p = *(const Shift * const *) a, *q = *(const Shift * const *) b;

  if (p->count != q->count)
    return p->count > q->count ? -1 : 1;
  if (p->dx != q->dx)
    return p->dx < q->dx ? -1 : 1;
  return (p->dy > q->dy) - (p->dy < q->dy);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *format = work_format (operation);
  GeglRectangle   bbox   = get_bounding_box (operation);
  gint            W = bbox.width, H = bbox.height;
  gint            factor, w, h, b = o->block_size, x, y, k;
  gfloat         *pix, *luma;
  gint32         *owner;
  GPtrArray      *kept = g_ptr_array_new ();
  gint            n_threads;

  (void) level;
  (void) result;
  if (gegl_rectangle_is_infinite_plane (&bbox) || W <= 0 || H <= 0)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      g_ptr_array_free (kept, TRUE);
      return TRUE;
    }
  g_object_get (gegl_config (), "threads", &n_threads, NULL);

  factor = (MAX (W, H) + o->analysis_size - 1) / o->analysis_size;
  factor = MAX (factor, 1);
  w = W / factor;
  h = H / factor;

  /* the luma, 0 to 255, reduced by the factor (box average) */
  pix  = g_new (gfloat, (gsize) W * H * 4);
  gegl_buffer_get (input, &bbox, 1.0, format, pix, GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);
  luma = g_new0 (gfloat, (gsize) MAX (w, 1) * MAX (h, 1));
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble s = 0;
        gint    i, j;

        for (j = 0; j < factor; j++)
          for (i = 0; i < factor; i++)
            {
              const gfloat *p = pix + ((gsize) (y * factor + j) * W + x * factor + i) * 4;
              gfloat        c[3];
              gint          n;

              for (n = 0; n < 3; n++)
                c[n] = p[n] == p[n] ? CLAMP (p[n], 0.0f, 1.0f) : 0.0f;
              s += 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            }
        luma[(gsize) y * w + x] = (gfloat) (s / (factor * factor) * 255.0);
      }
  /* reduced: smoothed a little more (1 2 1) / 4, so that a copy moved by
   * a fraction of the factor still looks alike */
  if (factor > 1 && w > 2 && h > 2)
    {
      gfloat *t = g_new (gfloat, (gsize) w * h);

      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          {
            const gfloat *l = luma + (gsize) y * w;

            t[(gsize) y * w + x] = 0.25f * l[MAX (x - 1, 0)] + 0.5f * l[x] +
                                   0.25f * l[MIN (x + 1, w - 1)];
          }
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          luma[(gsize) y * w + x] = 0.25f * t[(gsize) MAX (y - 1, 0) * w + x] +
                                    0.5f * t[(gsize) y * w + x] +
                                    0.25f * t[(gsize) MIN (y + 1, h - 1) * w + x];
      g_free (t);
    }

  owner = g_new0 (gint32, (gsize) MAX (w, 1) * MAX (h, 1));
  if (w >= b && h >= b)
    {
      Features    f;
      gsize       n_blocks, n_kept = 0, i;
      gint        bw = w - b + 1, bh = h - b + 1;
      GHashTable *shifts = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                                  g_free, g_free);
      Block      *list;
      gint        min_dist = o->min_distance > 0 ? o->min_distance : b;
      Marking     m;
      Pairs       pairs;
      GHashTableIter it;
      gpointer    value;

      memset (&f, 0, sizeof f);
      f.luma = luma;
      f.w = w;
      f.h = h;
      f.b = b;
      f.min_detail2 = o->min_detail * o->min_detail;
      for (k = 0; k < N_FREQ; k++)
        for (x = 0; x < b; x++)
          f.basis[k][x] = (k ? sqrt (2.0 / b) : sqrt (1.0 / b)) *
                          cos (G_PI * (2 * x + 1) * k / (2.0 * b));
      /* the quantisation: the tolerance in levels, times b (the size of
       * the orthonormal DCT of a b x b pattern of that amplitude), and
       * coarser for higher frequencies, where noise and compression
       * change more */
      for (y = 0; y < N_FREQ; y++)
        for (x = 0; x < N_FREQ; x++)
          f.step[y * N_FREQ + x] = o->tolerance * b * (1.0 + 0.5 * (x + y));
      f.rows   = g_new (gdouble, (gsize) w * h * N_FREQ);
      n_blocks = (gsize) bw * bh;
      f.blocks = g_new (Block, n_blocks);
      f.kept   = g_new (guint8, n_blocks);
      gegl_parallel_distribute_range (h, 4096.0 / MAX (w * b * N_FREQ, 1),
                                      row_dcts, &f);
      gegl_parallel_distribute_range (bh, 4096.0 / MAX (bw * b * b, 1),
                                      block_features, &f);
      /* the blocks with enough detail, sorted by their features */
      list = f.blocks;
      for (i = 0; i < n_blocks; i++)
        if (f.kept[i])
          list[n_kept++] = f.blocks[i];
      qsort (list, n_kept, sizeof (Block), cmp_block);

      pairs.luma      = luma;
      pairs.w         = w;
      pairs.h         = h;
      pairs.b         = b;
      pairs.min_dist2 = min_dist * min_dist;
      pairs.tolerance = o->tolerance;
      for_each_pair (list, n_kept, &pairs, count_pair, shifts);
      g_hash_table_iter_init (&it, shifts);
      while (g_hash_table_iter_next (&it, NULL, &value))
        if (((Shift *) value)->count >= o->min_matches)
          g_ptr_array_add (kept, value);
      g_ptr_array_sort (kept, cmp_shift_count);
      for (k = 0; k < (gint) kept->len; k++)
        ((Shift *) g_ptr_array_index (kept, k))->id = k + 1;
      if (g_getenv ("FORENSICS_CLONE_DEBUG"))
        for (k = 0; k < (gint) kept->len; k++)
          {
            Shift *sh = g_ptr_array_index (kept, k);

            g_printerr ("shift %d,%d: %d pairs, originals around %.0f,%.0f (factor %d)\n",
                        sh->dx, sh->dy, sh->count, sh->sx / sh->count,
                        sh->sy / sh->count, factor);
          }

      m.shifts = shifts;
      m.b = b;
      m.w = w;
      m.h = h;
      m.owner = owner;
      if (kept->len)
        for_each_pair (list, n_kept, &pairs, mark_pair, &m);

      /* the output, at full size; lines over the image */
      {
        gfloat (*colors)[3] = g_malloc (sizeof (gfloat[3]) * (kept->len + 1));

        for (k = 0; k < (gint) kept->len; k++)
          {
            Shift *s = g_ptr_array_index (kept, k);

            shift_color (s->dx, s->dy, colors[k]);
          }
        for (y = 0; y < H; y++)
          for (x = 0; x < W; x++)
            {
              gfloat *p  = pix + ((gsize) y * W + x) * 4;
              gint    ax = MIN (x / factor, w - 1), ay = MIN (y / factor, h - 1);
              gint    id = owner[(gsize) ay * w + ax];
              gint    n;

              if (o->mode == FORENSICS_CLONE_MASK)
                {
                  p[0] = p[1] = p[2] = id ? 1.0f : 0.0f;
                }
              else
                for (n = 0; n < 3; n++)
                  {
                    gfloat v = p[n] == p[n] ? CLAMP (p[n], 0.0f, 1.0f) : 0.0f;

                    p[n] = id ? 0.35f * v + 0.65f * colors[id - 1][n] : 0.35f * v;
                  }
              if (! (p[3] == p[3]))
                p[3] = 0.0f;
            }
        if (o->mode == FORENSICS_CLONE_OVERLAY && o->lines)
          for (k = 0; k < (gint) kept->len; k++)
            {
              Shift  *s  = g_ptr_array_index (kept, k);
              gdouble x0 = (s->sx / s->count + b / 2.0) * factor;
              gdouble y0 = (s->sy / s->count + b / 2.0) * factor;
              gdouble x1 = x0 + s->dx * factor, y1 = y0 + s->dy * factor;
              gint    steps = (gint) ceil (MAX (fabs (x1 - x0), fabs (y1 - y0))) + 1;
              gint    t, dx, dy, width = MAX (1, MAX (W, H) / 800);

              for (t = 0; t <= steps; t++)
                {
                  gint px = (gint) floor (x0 + (x1 - x0) * t / steps + 0.5);
                  gint py = (gint) floor (y0 + (y1 - y0) * t / steps + 0.5);

                  for (dy = -width / 2; dy <= width / 2; dy++)
                    for (dx = -width / 2; dx <= width / 2; dx++)
                      if (px + dx >= 0 && px + dx < W && py + dy >= 0 && py + dy < H)
                        {
                          gfloat *p = pix + ((gsize) (py + dy) * W + px + dx) * 4;

                          p[0] = colors[k][0];
                          p[1] = colors[k][1];
                          p[2] = colors[k][2];
                        }
                }
            }
        g_free (colors);
      }
      g_free (f.rows);
      g_free (f.blocks);
      g_free (f.kept);
      g_hash_table_destroy (shifts);
    }
  else
    {
      /* smaller than a block: nothing to compare */
      for (x = 0; x < W * H; x++)
        {
          gfloat *p = pix + (gsize) x * 4;
          gint    n;

          for (n = 0; n < 3; n++)
            p[n] = o->mode == FORENSICS_CLONE_MASK ? 0.0f
                   : 0.35f * (p[n] == p[n] ? CLAMP (p[n], 0.0f, 1.0f) : 0.0f);
          if (! (p[3] == p[3]))
            p[3] = 0.0f;
        }
    }

  gegl_buffer_set (output, &bbox, 0, format, pix, GEGL_AUTO_ROWSTRIDE);
  g_ptr_array_free (kept, TRUE);
  g_free (owner);
  g_free (luma);
  g_free (pix);
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                   = prepare;
  operation_class->get_bounding_box          = get_bounding_box;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->get_cached_region         = get_cached_region;
  operation_class->opencl_support            = FALSE;
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "forensics:clone-detect",
    "title",           _("Clone Detection"),
    "categories",      "forensics:analysis",
    "description",     _("Finds parts of the image copied to another place "
                         "of it (copy-move, the clone tool), after Fridrich "
                         "et al. 2003. Repeating patterns match too. An "
                         "indicator, not proof."),
    "gimp:menu-path",  "<Image>/Filters/Forensics",
    "gimp:menu-label", _("Clone Detection..."),
    NULL);
}

#endif
