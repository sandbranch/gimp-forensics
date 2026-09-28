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

Second round, after Sherloq (2026-09-28):

- `forensics:wavelet-noise` (Mahdian and Saic 2009, Sherloq's Wavelet
  Blocking), `forensics:minmax` (Min/Max Deviation, with a density view),
  `forensics:bit-plane`, `forensics:echo` (Echo Edge Filter, OpenCV's
  kernels), `forensics:median-detect` (Kirchner and Fridrich 2010 instead
  of Sherloq's XGBoost model), `forensics:resampling` (Popescu and Farid
  2005 with Kirchner's fixed predictor instead of Sherloq's EM); shared
  helpers in operations/forensics-common.h.
- JPEG Info (Image > Forensics > JPEG Info...): the quality of the last
  save from the tables (IJG, mozjpeg's tables, Sherloq's estimate),
  JPEGsnoop's signatures and assessment, double compression from the
  file's own DCT coefficients (a pure Python decoder, the same numbers as
  libjpeg), a double JPEG map (after Bianchi and Piva 2012), metadata of
  software, the Exif thumbnail against the image and as layers.
- Workbench: the new filters, JPEG Info's map and thumbnail, and the
  file's qualities for Error Level Analysis and JPEG Ghost.
- Tests: 247 operation checks, 179 GIMP filter checks, the JPEG Info
  checks, 21 Workbench checks, 17 GUI checks; new sample edits
  (lake-resampled, lake-median) and their numbers in samples/README.md.
- Evaluated and not built: Sherloq's contrast enhancement (does not
  separate a gamma change from ordinary blocks; gone after a JPEG save),
  its Composite Splicing (Noiseprint: a neural network under a
  non-commercial licence), its compression model (machine learning).

## Next

1. **Automatic grid offset** for ELA and JPEG Ghost: find the offset of
   the original JPEG grid from the image (the blockiness of the 8 x 8
   lattice, as Farid did by trying all 64 shifts), shown in the dialog or
   as an "auto" choice.
2. **Clone Detection**: rotated, scaled and mirrored copies (Zernike or
   log-polar features); a report of the shifts found (count, size) in the
   dialog, as the LUT filter shows its errors.
3. **Noise**: a robust local noise level: done in the second round
   (`forensics:wavelet-noise`, Mahdian and Saic 2009).
4. **JPEG Ghost**: Farid's K-S statistic for a selection, and masking of
   low variance blocks (his 2.5 gray values), in a small report plug-in.
5. Workbench: settings of the level sweep and PCA in the dialog as
   presets; a second run that updates the analyses in place instead of
   adding a group.
6. **PRNU plug-in** (camera identification from the sensor's pattern
   noise, as Sherloq's PRNU Identification and J. Lukas, J. Fridrich and
   M. Goljan, "Digital camera identification from sensor pattern noise",
   IEEE TIFS 1 (2), 2006): needs reference images from the camera in
   question, so a plug-in with its own dialog (choose the reference
   images, build the fingerprint, correlate), not a filter.
7. **JPEG Info**: the double JPEG map picks up small regions weakly (the
   plane of lake-splice-plane.jpg: 8 % of its blocks); a calibrated
   envelope (Lukas and Fridrich's cropping by 4 pixels, which needs an
   IDCT and a DCT of the file) would sharpen both the global test and the
   map. A detection of a first save finer than the last (q1 < q2).
   Nonaligned double compression (a crop between the saves: the grid
   found by blockiness, then the same model).
8. **Resampling and median detection on JPEG files**: the published
   JPEG-robust variants (for median filtering, SPAM features of Kirchner
   and Fridrich 2010, which need a trained classifier).
9. Offer the operations to GEGL (`gegl:error-level` and so on) once they
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
| Thumbnail analysis | Forensically, Sherloq | no (the embedded Exif thumbnail is not shown) | done: JPEG Info (comparison, Difference layers) |
| JPEG analysis (quantization tables, estimated quality, comments) | Forensically, elsamuko's jpegqual, Sherloq, JPEGsnoop | GIMP's JPEG import reads the quality for "use original quality" but does not show the tables | done: JPEG Info |
| Content Credentials (C2PA) | Forensically | no | plug-ins/content-credentials (in progress, separately) |

## Found along the way (to report upstream)

- GIMP 3.2.6 `file-jpeg-export` (plug-ins/file-jpeg/jpeg-export.c): the
  `restart` argument is used only when the auxiliary `use-restart` is
  set (`cinfo.restart_in_rows = use_restart ? restart : 0`), and a script
  cannot set an auxiliary argument, so scripts cannot write restart
  markers. In gimp-console the export also wrote no Exif thumbnail with
  `include-exif` and `include-thumbnail` set (why was not looked into).
  The JPEG Info tests use jpegtran and their own Exif instead.

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
