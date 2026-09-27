# Sample images

Images to try the forensics tools on: real manipulated photographs, edits
made here with a known change at a known place, files with Content
Credentials, and untouched photos as controls. All are free: public
domain, CC0, CC BY or CC BY-SA, each licence checked on its source page
(2026-09-27). None is a personal photo.

The images are not in git. To get them:

    samples/fetch-samples.py     # downloads into samples/images, checks SHA-256, writes CREDITS.txt
    samples/make-edits.py        # makes the edited images (Pillow and numpy)
    samples/measure.py           # optional: the numbers below (GEGL in the isolated GIMP Flatpak)

Then open `samples/images` in GIMP. Each filter is under **Filters >
Forensics**; **Image > Forensics > Analyze Image...** (the Workbench)
puts them all on one image as layers; **Image > Forensics > Content
Credentials...** reads the file's credentials.

`fetch-samples.py` needs only Python 3. `make-edits.py` and `measure.py`
need Pillow and numpy. `measure.py` runs the operations built in `build/`
with the gegl command line inside the GIMP Flatpak, isolated as the tests
are (`tests/isolate.sh`), and prints the mean of each result inside the
edited region, in the rest of the image and in the same region of the
unedited photo, with the share of the region's 16 x 16 blocks beyond the
99th (or below the 1st) percentile of the other blocks. Its output is in
`tests/output/samples/measure.txt`. The numbers below are from that run,
with the default settings of each filter unless a setting is named (ELA
at quality 90 and scale 20, JPEG Ghost at the quality named, Noise
Analysis as luminance averaged over 32 pixels, Clone Detection as a
mask). ELA and noise values are in 8 bit levels of the result (0 to 255),
the others from 0 to 1.

What these numbers are not: proof. As the main [README](../README.md)
says, each tool shows a property of the pixels that differs for many
innocent reasons too. Several rows below say plainly where a real, known
manipulation does not show.

## Real manipulated photographs

From Wikimedia Commons (categories "Manipulated photographs", "Altered
Soviet photographs", "Photomontage step by step").

| File | Source | Author | Licence | What it is | Try | What the tools show (measured) |
|---|---|---|---|---|---|---|
| `sogndal-composite.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Fotomanipulasjon,_dampskipskaien_i_Sogndal_utan_kaibygg.jpg) | Bjørn Erik Pedersen | CC BY 4.0 | Sogndal quay with the big red building removed; the author says it is a composite "to illustrate how Sogndalsfjøra could look like without the big red building", partly from other photos | ELA, Noise, Clone Detection; compare with `sogndal-original.jpg` | ELA: the sky and cloud where the building stood are brighter and blocky, 1.48 times the rest (x 780 to 2340, y 680 to 1610), clearly visible. Noise: the same area 2.1 times the rest. Clone Detection marks 2.7 % of the image, pairs in the sand, the water and the right edge (the original: 0.26 %); the steps of the edit are not documented, so whether these are clone brush strokes was not confirmed |
| `sogndal-original.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Dampskipskaien_i_Sogndal_2016.jpg) | Bjørn Erik Pedersen | CC BY 4.0 | the same view with the building (Pentax K-3 II, Lightroom export) | ELA | even ELA (mean 20.4) with bright edges only, as in the controls |
| `pin-concealed.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Concealed_PIN_number.jpg) | McGeddon | CC0 | a PIN letter: the real number showing through the paper removed and a made-up "2301" drawn in, made from `pin-original.jpg` | ELA, Noise | ELA: the digits "2301" glow, 10.9 times the rest of the image (20 times the same pixels in the original); Noise: 3.5 times. Where the old number was removed is not separated from the new digits (they overlap). Clone Detection marks 1.7 %, along the perforated top edge: a repeated pattern, not the edit |
| `pin-original.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:PIN-Brief.jpg) | Mattes | public domain (released by the author) | the letter before the edit | ELA | even, mean 5.2 |
| `ptz-montage.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Montage_Ptz_6_final_Nata_and_double_rainbow_at_Chapayev_street_Ptz_2012.jpg) | Andrew Krizhanovsky | public domain (released by the author) | a portrait with a rose and a double rainbow set into a street view; the steps are in [Photomontage step by step](https://commons.wikimedia.org/wiki/Category:Photomontage_step_by_step) | ELA, Clone Detection | ELA: the pasted portrait and the rainbow bands stand out in color; the portrait area (x 1540 to 2040, y 400 to 830) is 1.56 times the rest, 40 % of its blocks above the 99th percentile. Clone Detection marks 3.5 %, mostly along the power lines (straight lines match along themselves): read with care |
| `yezhov-original.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Nikolai_Yezhov_with_Stalin_and_Molotov_along_the_Volga%E2%80%93Don_Canal,_original.jpg) | unknown photographer, 1937 | public domain (PD-Russia-1996 on Commons) | Voroshilov, Molotov, Stalin and Nikolai Yezhov at the Moscow Canal | ELA, Noise | the reference for the next one |
| `yezhov-removed.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Stalin_and_Molotov_along_the_Volga%E2%80%93Don_Canal,_Nikolai_Yezhov_removed.jpg) | unknown, 1937; retouched later | public domain (PD-Russia-1996 on Commons) | the same photo with Yezhov painted out, a scan of a print | Noise, ELA | **Does not show clearly.** The retouching was done by hand on a print; the tools see the scan and its JPEG. The painted water on the right (x 760 to 1200, y 330 to 900) is somewhat smoother: noise 0.74 times the rest, where the same area of the original is 1.07 times; ELA 0.82 times. The two files are different prints, framed differently, so this is a hint at most |
| `lincoln-calhoun-composite.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Manipulated_portrait_of_Abraham_Lincoln_%281860%27s%29.jpg) | unknown, 1860s | public domain (PD-US) | Lincoln's head on John Calhoun's body, a 19th century composite print, photographed | ELA, Luminance Gradient | **Does not show.** ELA is even (mean 11.2), no Clone Detection marks: the join was made on paper 160 years ago, and the file is a photograph of the print |
| `kirksville-fake-tornado.png` | [Commons](https://commons.wikimedia.org/wiki/File:1899_Kirksville,_Missouri_fake_tornado_photo.png) | unknown; scan via the Library of Congress | public domain (PD-US-expired) | a faked tornado photo of Kirksville, Missouri, 1899, the funnel painted onto the print | ELA, Noise | **Does not show.** ELA shows only the tree branches (mean 5.6), nothing marks the painted funnel; the file is a PNG saved by GIMP 2.10 from a book scan |

## Edits made here (`make-edits.py`)

From three CC0 photos (below, "Controls and sources"). Each edit is one
change; `*-original.jpg` is the same photo saved the same way (Pillow,
quality 90, 4:2:0) without it, and `images/masks/<name>-mask.png` shows
where the change is (white). The coordinates are pixels from the top
left of the full image.

| File | Made from | What was done, and where | Try | What the tools show (measured) |
|---|---|---|---|---|
| `lake-original.jpg` | `ribnica-lake-camera.jpg` | the camera JPEG (its tables are those of quality 97 or higher, 4:2:2) saved again at quality 90 | all | the reference for the lake edits: ELA mean 0.68 (dark: it was just saved at 90); Clone Detection marks nothing |
| `lake-splice-plane.jpg` | `ribnica-lake-camera.jpg`, `airplane-camera.jpg` | the airliner of the Kodak photo with 8 pixels of its own grey sky, colors moved to the blue sky, saved at **quality 60** on the 8 x 8 grid it lands on, pasted at x 1152, y 672 (in a 368 x 160 piece, the mask is the plane and its margin), the whole saved at 90 | ELA; JPEG Ghost at quality 60; Noise | ELA: **the pasted plane is brighter**, 10.2 times the rest and 13.8 times the same sky unedited, 49 % of its blocks above the 99th percentile. JPEG Ghost at 60: the plane is dark, 0.23 against 0.77, 81 % of its blocks below the 1st percentile. Noise: 2.9 times the rest (the Kodak sky is noisy). The Ghost's "quality of the smallest difference" map shows 90 there as everywhere: the last save wins; look at the normalised difference at 60. PCA (third component) shows nothing (0.84 times) |
| `lake-clone.jpg` | `ribnica-lake-camera.jpg` | a 400 x 400 piece of dwarf pines from x 2820, y 1668 copied to x 2052, y 2086, over the snow at the bottom, with a 12 pixel soft edge | Clone Detection | **found**: 84 % of the source and 84 % of the copy marked, nothing else (0.00 %). ELA shows nothing (0.87 times the rest): a copy from the same image has the same compression history |
| `lake-airbrush.jpg` | `ribnica-lake-camera.jpg` | the cloud at the right, an ellipse centred at x 3080, y 760, radii 480 x 200, blurred (Gaussian, radius 4) with a 24 pixel soft edge | Noise Analysis | Noise: the ellipse is **smoother**, 0.09 times the rest and 0.33 times the same cloud unedited, 95 % of its blocks below the 1st percentile of the others (a cloud is smooth anyway, hence the 0.33). ELA: a little darker, 0.69 times the rest |
| `lake-double-jpeg.jpg` | `ribnica-lake-camera.jpg` | the whole photo saved at quality 70, then again at 90; nothing else | JPEG Ghost at 70 | JPEG Ghost at 70: **the whole image is dark**, normalised difference 0.06 against 0.56 for `lake-original.jpg` (saved once at 90). There is no edited region to compare: everything was compressed twice. ELA mean 0.56 against 0.68 |
| `eggs-original.jpg` | `eggs-camera.jpg` | the photo saved at quality 90 | Luminance Gradient | the reference |
| `eggs-mirrored-egg.jpg` | `eggs-camera.jpg` | the large front egg, second from the left: its shell (by color) inside an ellipse centred at x 1505, y 1735, radii 380 x 440, replaced by its mirror image about x 1505, so its highlight moves from the upper left to the upper right; 6 pixel soft edge | Luminance Gradient | **Does not show clearly.** The mean of the normal map's red (the x slope) over the egg goes from 0.5012 to 0.5000; the other front eggs are 0.4999 to 0.5025. The light is soft and the egg nearly round, so the left and right halves differ mostly by the egg's shape (0.497 left, 0.503 right, before and after). The edit is plain to the eye (a sliver at the lower left shows); the tool does not single it out |

## Content Credentials

For **Image > Forensics > Content Credentials...**. The verdict is what the
plug-in's own report (`plug-ins/content-credentials/c2pa_report.py`, run
outside GIMP with c2pa-rs 0.91.0, offline) gave on each file.

| File | Source | Author | Licence | What it is | Verdict (measured) |
|---|---|---|---|---|---|
| `firefly-landscape.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Adobe_Firefly_generated_landscape_photography.jpg) | Adobe Firefly Image 3, prompted by Jiří Vedral | public domain (PD-algorithm) | an Adobe Firefly image | **VALID**, signer "firefly-prod" (Adobe Inc.), chains to the Interim Trust List, signed 2024-06-14; generative AI: created, trainedAlgorithmicMedia |
| `dalle3-crossword-globe.webp` | [Commons](https://commons.wikimedia.org/wiki/File:Crossword_puzzle_in_shape_of_globe_made_of_puzzle_pieces,_dall-e_3.webp) | DALL-E 3 in ChatGPT, prompted by JPxG | public domain (PD-algorithm) | a ChatGPT / DALL-E 3 image | **VALID**, "Truepic Lens CLI in ChatGPT" (OpenAI), Interim Trust List, 2024-02-12; its ingredient, signed by DALL-E, declares trainedAlgorithmicMedia |
| `gemini-generated.png` | [Commons](https://commons.wikimedia.org/wiki/File:Gemini_Generated_Image_yp81eoyp81eoyp81.png) | Zhaotian201512, made with Google Gemini | CC BY-SA 4.0 | a Google Gemini image | **VALID**, "Google Media Processing Services" (Google LLC), 2026-03-04, four manifests; created and edited with generative AI. The trust list named changes from run to run (C2PA Trust List or C2PA TSA Trust List): Google's root certificate is on both, and c2pa-rs reports either |
| `copilot-designer-cat.png` | [Commons](https://commons.wikimedia.org/wiki/File:Gato_naranja_generado_por_IA.png) | DALL-E 3 through Microsoft Copilot | public domain (PD-algorithm; also CC BY 4.0) | a Microsoft Designer image, three manifests | **INVALID**: Microsoft's signing certificate is on no trust list here and does not validate (signingCredential.untrusted, signingCredential.invalid), in the image and its ingredients |
| `phone-photo-adobe-cai.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Content-Authenticity-Initiative-Photos-RuinDig_002.jpg) | RuinDig/Yuki Uchida | CC0 | a phone photo (EXIF: samsung SCG15; the page says Xiaomi 14T Pro) credentialed afterwards with Adobe Content Authenticity | **INVALID**: c2pa-rs itself rates it invalid because the CAWG identity's DID cannot be resolved offline (cawg.ica.did_unavailable); signed by "Adobe Content Authenticity", 2025-05-26; actions opened and watermarked; ingredient without credentials |
| `gemini-xmp-only.png` | [Commons](https://commons.wikimedia.org/wiki/File:Caneta_3d.png) | Google Gemini | public domain (PD-algorithm; also CC0) | a Gemini image without a C2PA manifest | **NONE**, with "Generative AI: trainedAlgorithmicMedia, says the XMP metadata, which is not signed" |
| `tests/content-credentials/images/adobe-20220124-C.jpg` | [c2pa-org/public-testfiles](https://github.com/c2pa-org/public-testfiles) | C2PA | CC BY-SA 4.0 | a test file with one claim (by reference: fetched by `tests/content-credentials/fetch-images.py`) | **UNKNOWN SIGNER** (valid, test certificate) |
| `tests/content-credentials/images/adobe-20220124-E-sig-CA.jpg` | same | C2PA | CC BY-SA 4.0 | a test file whose claim signature was broken | **TAMPERED** (claimSignature.mismatch) |
| `tests/content-credentials/images/nikon-20221019-building.jpeg` | same | C2PA (Nikon Z 9 sample for Adobe MAX 2022) | CC BY-SA 4.0 | a camera-signed photo | **INVALID**: signer "NIKON CORPORATION", certificate expired when it signed and on no trust list |

Every other sample here gives **NONE**, the edits too: Pillow writes no
credentials, and neither does GIMP's export.

Signed images straight from other cameras (Leica, Sony) were looked for briefly;
no sample with terms that allow redistribution was found, so none is
included. The Nikon file above is the camera example.

## Controls and sources

Untouched files, to see what "nothing suspicious" looks like, and the
photos the edits are made from.

| File | Source | Author | Licence | What it is | What the tools show (measured) |
|---|---|---|---|---|---|
| `oranges-camera.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Oranges_-_DSC06058.JPG) | Daderot | CC0 | camera JPEG, Sony DSC-RX100 (its firmware in EXIF Software) | ELA even, bright on edges and texture (mean 16.6): a high quality camera file changes a lot when saved at 90. Clone Detection marks 0.7 %, along the straight edge of the tray: a false positive |
| `lake-colorado-camera.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Landscape_of_lake_and_clouds.jpg) | Mshuang2 | CC0 | phone JPEG, OnePlus A6003 | ELA even (mean 15.1) |
| `polyhaven-rocks-lossless.png` | [Poly Haven](https://polyhaven.com/a/aerial_rocks_02) | Rob Tuytel | CC0 | a photo texture published as PNG (never saved as JPEG by its publisher; its processing is not documented) | ELA bright everywhere (mean 67.3): never compressed fine detail changes most. JPEG Ghost: smallest difference at 95, the top of the sweep, everywhere (no ghost). Clone Detection marks 21.6 %: the texture has repeated parts, and repeats are what it finds |
| `ribnica-lake-camera.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Ribnica_Lake_1.jpg) | Janezdrilc | CC0 | camera JPEG, Nikon Coolpix L23: source of the lake edits | ELA even (mean 18.2); JPEG Ghost smallest at 95 |
| `airplane-camera.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Airplane,_sep_6_2026.jpg) | Logan M Diel | CC0 | camera JPEG, Kodak Z812 IS: the plane of the splice | |
| `eggs-camera.jpg` | [Commons](https://commons.wikimedia.org/wiki/File:Eggs_Chicken.jpg) | safaritravelplus | CC0 (licence review on Commons) | Canon EOS 250D photo: source of the egg edit | |

Controls compared with the edits: after one save at quality 90 the lake
photo's ELA is dark and even (mean 0.68), and only a region with another
history (the plane) is bright. The camera originals are bright and even
instead. What to look for is a difference between regions, not a
brightness.

## Licences and credits

`images/CREDITS.txt` (written by `fetch-samples.py`) lists every file
with its author, licence and source; `samples.json` has the same with
the SHA-256 of each file. CC BY 4.0 (Bjørn Erik Pedersen) and CC BY-SA
4.0 (Zhaotian201512) need that attribution when you pass the files on;
the edited images derive only from CC0 photos. The scripts are
GPL-3.0-or-later like the rest of the repository.
