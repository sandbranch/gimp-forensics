/*
 * The quantised DCT coefficients of a JPEG file, as libjpeg reads them
 *
 * jpeg-coefficients.c
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   jpeg-coefficients <file.jpg> [rows]
 *
 * Prints the size of each component in blocks, then the coefficients of
 * the first component (luma), block by block in raster order, 64 numbers
 * per line in natural (row by row) order; only the first rows of blocks
 * if given. The reference for the pure Python decoder of the JPEG Info
 * plug-in (tests/jpeg-info/unit.py compares the two, baseline and
 * progressive files alike). jpeg_read_coefficients () gives the
 * coefficients exactly as they are in the file, before any IDCT.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>

struct err
{
  struct jpeg_error_mgr pub;
  jmp_buf               jump;
};

static void
error_exit (j_common_ptr cinfo)
{
  struct err *e = (struct err *) cinfo->err;

  (*cinfo->err->output_message) (cinfo);
  longjmp (e->jump, 1);
}

int
main (int    argc,
      char **argv)
{
  struct jpeg_decompress_struct cinfo;
  struct err                    jerr;
  jvirt_barray_ptr             *coefs;
  FILE                         *f;
  JDIMENSION                    rows, r, c;
  int                           ci, k;

  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <file.jpg> [rows]\n", argv[0]);
      return 2;
    }
  f = fopen (argv[1], "rb");
  if (! f)
    {
      perror (argv[1]);
      return 1;
    }
  cinfo.err = jpeg_std_error (&jerr.pub);
  jerr.pub.error_exit = error_exit;
  if (setjmp (jerr.jump))
    {
      jpeg_destroy_decompress (&cinfo);
      fclose (f);
      return 1;
    }
  jpeg_create_decompress (&cinfo);
  jpeg_stdio_src (&cinfo, f);
  jpeg_read_header (&cinfo, TRUE);
  coefs = jpeg_read_coefficients (&cinfo);
  printf ("components %d\n", cinfo.num_components);
  for (ci = 0; ci < cinfo.num_components; ci++)
    printf ("blocks %u %u\n", cinfo.comp_info[ci].width_in_blocks,
            cinfo.comp_info[ci].height_in_blocks);
  rows = cinfo.comp_info[0].height_in_blocks;
  if (argc > 2 && (JDIMENSION) atoi (argv[2]) < rows)
    rows = (JDIMENSION) atoi (argv[2]);
  for (r = 0; r < rows; r++)
    {
      JBLOCKARRAY row = (*cinfo.mem->access_virt_barray) ((j_common_ptr) &cinfo,
                                                          coefs[0], r, 1, FALSE);

      for (c = 0; c < cinfo.comp_info[0].width_in_blocks; c++)
        for (k = 0; k < DCTSIZE2; k++)
          printf ("%d%c", row[0][c][k], k == DCTSIZE2 - 1 ? '\n' : ' ');
    }
  jpeg_finish_decompress (&cinfo);
  jpeg_destroy_decompress (&cinfo);
  fclose (f);
  return 0;
}
