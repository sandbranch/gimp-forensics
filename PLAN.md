# Plan

## Done

- `forensics:error-level`: Error Level Analysis, a GEGL filter (README).
  The in-memory JPEG round trip gives exactly the pixels of a round trip
  of the whole image; a JPEG exported by GIMP with the same settings
  decodes to the same pixels.
- `forensics:jpeg-ghost`: Farid's JPEG ghosts, with his normalisation,
  the best quality map and the GIMP 2 plug-in's color ramp.
- `forensics:noise`: noise residual (median or wavelet), local level,
  auto levels.
- `forensics:luminance-gradient`: normal map, direction as hue,
  magnitude.
- `forensics:clone-detect`: copy-move detection after Fridrich et al.,
  with pixel verification and the rejection of self-similar blocks
  (straight edges).
- `forensics:pca`: principal components of the colors, for the
  Workbench.
- Forensics Workbench (Image > Forensics > Analyze Image...), a Python 3
  plug-in.
- Tests: 112 operation checks (also under ASan and UBSan), 110 GIMP
  filter checks, 14 Workbench checks, 10 GUI checks on Broadway, timings.

## Next

1. **Automatic grid offset** for ELA and JPEG Ghost: find the offset of
   the original JPEG grid from the image (the blockiness of the 8 x 8
   lattice, as Farid did by trying all 64 shifts), shown in the dialog or
   as an "auto" choice.
2. **Clone Detection**: rotated, scaled and mirrored copies (Zernike or
   log-polar features); a report of the shifts found (count, size) in the
   dialog, as the LUT filter shows its errors.
3. **Noise**: a robust local noise level (the median absolute deviation
   of the finest wavelet details per block, Mahdian and Saic 2009), which
   texture disturbs less than the mean used now.
4. **JPEG Ghost**: Farid's K-S statistic for a selection, and masking of
   low variance blocks (his 2.5 gray values), in a small report plug-in.
5. Workbench: settings of the level sweep and PCA in the dialog as
   presets; a second run that updates the analyses in place instead of
   adding a group.
6. Offer the operations to GEGL (`gegl:error-level` and so on) once they
   have been in use: GEGL has no forensics operations.

## Other tools: what GIMP 3 already has, and what is worth adding

From elsamuko's forensics tools (github.com/elsamuko/forensics, slides
of Kielux 2013; the tools GPL-3+, the slides CC BY 3.0) and Forensically
(29a.ch/photo-forensics):

| Tool | Where | In GIMP 3 | Here |
|---|---|---|---|
| Error Level Analysis | elsamuko, Forensically, GIMP-ELA | no | done |
| JPEG Ghost | Gimp Forensics (SourceForge) | no | done |
| Clone detection | elsamuko (elsamuko-copy-move.cpp, block DCT), Forensically | no | done |
| Noise analysis | Forensically | no | done |
| Luminance gradient | Forensically | no | done |
| PCA | Forensically, Krawetz 2007 | no | done |
| Level sweep | Forensically | Colors > Levels or Curves by hand, one band at a time | Workbench preset (gegl:levels, edit the filter to sweep) |
| Magnifier with enhancement | Forensically | View > Zoom with Colors > Auto > Stretch Contrast, Equalize | covered |
| HSV analysis | elsamuko (elsamuko-hsv-analysis.c) | the channels: Colors > Components > Decompose (HSV); not its 2D hue-saturation histogram | channels as a Workbench preset; **the 2D histogram is worth adding** (a plug-in that makes a new image, not a filter) |
| LAB analysis | elsamuko (elsamuko-lab-analysis.c) | the channels: Decompose (LAB); not its a-b histogram | as HSV |
| Up Down Curve | elsamuko (elsamuko-up-down.scm: a curve going up and down 7 times, then a blur of 10) | Colors > Curves by hand, then Gaussian Blur | **worth adding** as a Workbench preset once a curve can be set on a filter from a script (gimp:curves is not a GEGL operation a DrawableFilter can take today); or a small operation |
| Metadata | Forensically | Image > Metadata > View / Edit Metadata (Exif, XMP, IPTC) | covered |
| Geo tags | Forensically | the GPS fields in the metadata viewer; no map | covered as far as needed |
| Thumbnail analysis | Forensically | no (the embedded Exif thumbnail is not shown) | **worth adding**: extract it (GExiv2) and show it and its difference with the image |
| JPEG analysis (quantization tables, estimated quality, comments) | Forensically, elsamuko's jpegqual | GIMP's JPEG import reads the quality for "use original quality" but does not show the tables | **worth adding**: a report of the tables and the estimated quality |
| Content Credentials (C2PA) | Forensically | no | plug-ins/content-credentials (in progress, separately) |

## Found along the way (to report upstream)

- GEGL `operations/external/jpg-load.c`: `gegl_jpg_load_query_jpg`
  reads the header and calls `jpeg_destroy_decompress` without
  `jpeg_abort_decompress` (or `jpeg_finish_decompress`), so the source
  manager's `term_source` never runs and its 1024 byte buffer leaks on
  every query (seen with LeakSanitizer in tests/check.sh; confirmed in
  GEGL master).
- The `gegl` command line reports "1 GeglBuffers leaked" for area
  filters, gegl:gaussian-blur and gegl:stretch-contrast among them, not
  only for these operations.
- GIMP 3.2 on Broadway places its image window above and left of the
  page: `gimp_session_info_apply_geometry` clamps the position into the
  monitor's work area, which is 0 x 0 there, and a negative position in
  sessionrc means right aligned. tests/gui/common.sh moves the window
  back through the page's Broadway client (`cmdMoveResizeSurface`). A
  note for gimp-devtools.
- GIMP's filter dialog leaves out a property named `output` (it clashes
  with the operation's output pad): named `mode` here.
