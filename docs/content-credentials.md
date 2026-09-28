# Content Credentials

**Image > Forensics > Content Credentials...** shows the Content
Credentials (C2PA provenance) of the file an image was opened from, and
whether they check out. Read only: nothing in the image or the file is
changed.

Content Credentials are a signed record inside the file (the C2PA
standard, [c2pa.org](https://c2pa.org)): who signed it, when, with which
software, what was done to the image (the actions), what it was made from
(the ingredients, which can have credentials of their own), and what kind
of source it is (the IPTC digital source type, which is where generative
AI is declared). OpenAI, Adobe Firefly and Photoshop, Google and some
cameras write them.

## What the dialog shows

- **The verdict**, in plain words, with a coloured badge:

  | Badge | Means |
  |---|---|
  | VALID | the signature is valid, the file is unchanged since signing, and the signer's certificate chains to a trust list (which one is named) |
  | UNKNOWN SIGNER | valid and unchanged, but the signer is on no trust list the plug-in has: anyone can make such a certificate (all test files are like this) |
  | TAMPERED | the image data is not what was signed, or the credentials themselves were changed after signing |
  | INVALID | other problems, for example the certificate had expired when it signed |
  | NONE | the file has no C2PA manifest store |
  | ONLINE ONLY | the file points to credentials on a server; they are not fetched (see "Offline") |
  | NOT CHECKED | the C2PA library is missing, or the file type is not a C2PA format (XCF) |

- **Generative AI**, when the credentials (or the XMP metadata) say so,
  with the IPTC term, its official label and definition, and where the
  finding came from: which manifest, which action, signed by whom; or
  "XMP metadata, not signed". Flagged terms:
  `trainedAlgorithmicMedia` (Created using Generative AI),
  `compositeWithTrainedAlgorithmicMedia` (Edited using Generative AI),
  `compositeSynthetic` (Composite including generative AI elements), and
  as "may include generative AI" `composite` and `virtualRecording`.
  `algorithmicMedia` (pure algorithmic media, no training data) is not
  flagged. The labels and definitions are IPTC's (NewsCodes Digital Source
  Type, CC BY 4.0), plus C2PA's own `empty` and `trainedAlgorithmicData`.
- **A tree** with the active manifest: claim generator, signer and issuer,
  time of signing (from the trusted time stamp, if there is one), authors
  as stated, the actions (`c2pa.created`, `c2pa.opened`, `c2pa.placed`,
  `c2pa.edited` and the others, in words, with the software agent and the
  digital source type), and the ingredients, each with its relationship
  (the file this one was made from, a file placed into it), whether its
  signer is trusted, its problems, and its own manifest below it, as deep
  as the credentials go. Then every validation finding with its C2PA
  status code, the XMP findings with their source, where in the file the
  manifest store is (JPEG APP11, PNG caBX, WebP C2PA chunk, ISO BMFF uuid
  box for HEIF/AVIF, JPEG XL jumb box, TIFF tag 52545, GIF application
  extension), and the library and trust lists used.
- **Copy Report** puts the whole report on the clipboard as text.

The check is against **the file on disk**, not the image as it is in GIMP:
after any edit in GIMP the pixels no longer match the signed hash, but the
file still does, and the dialog says so when the image has been changed.

Without a C2PA manifest the XMP metadata is still read: the IPTC
`DigitalSourceType` (as GIMP read it with the image, or from the file),
`dcterms:provenance` and `xmp:CreatorTool`. If the XMP refers to
credentials (`dcterms:provenance`) that are not in the file, the dialog
says they were probably removed by software that does not keep them;
GIMP's own export does exactly that (see
[content-credentials-plan.md](content-credentials-plan.md)).

## What it does not tell you

- **Credentials say who signed, not that the content is true.** A valid
  signature from a trusted signer means that this signer made this claim
  about this file. A camera can photograph a screen; a person can sign a
  lie. Judge the signer.
- **No credentials proves nothing.** Most images never had any, and most
  software (GIMP 3.2 included, tested) drops them when it saves. A
  screenshot or a re-save removes them from an AI image too.
- **XMP is not signed.** Anyone can write `trainedAlgorithmicMedia` into
  a real photo, or remove it from an AI image; the dialog labels XMP
  findings as unsigned.
- **UNKNOWN SIGNER is not "fake".** It means the plug-in cannot tell who
  holds the key. Test certificates, self-signed tools and products that
  are not (yet) in the C2PA Conformance Program all look like this.

### Why there is no pixel-based AI detection

The plug-in does not look at the pixels to guess whether an image was
made by AI, on purpose:

- Such detectors are classifiers trained on the generators of their day.
  They go out of date with each new model, fail on resized, recompressed
  or edited images, and give false positives on real photos (heavily
  processed phone pictures, illustrations, scans), with no way for the
  user to check the answer.
- A score from a detector looks like evidence and is not. Next to signed
  credentials, whose meaning is exact and checkable, it would blur what
  the dialog can honestly say.
- Invisible watermarks (Google's SynthID, Adobe TrustMark, Digimarc and
  others, which C2PA calls soft bindings) need each vendor's own decoder,
  mostly closed or online. Recovering credentials through them means
  looking a watermark up in an online manifest repository, which this
  plug-in does not do (see "Offline").

## Offline

The plug-in never goes online. The C2PA library is set to fetch neither
remote manifests nor certificate revocation (OCSP) and to allow no network
host at all; revocation information stapled into the file is used. Trust
is decided against the trust lists that come with the plug-in, in
`plug-ins/content-credentials/trust/` (see `SOURCES.txt` there):

- the **C2PA Trust List** of the C2PA Conformance Program, and the
  **C2PA TSA Trust List** of time stamp authorities
  ([c2pa-org/conformance-public](https://github.com/c2pa-org/conformance-public),
  CC BY 4.0);
- the **Interim Trust List** that contentcredentials.org/verify used from
  2021 to 2025, frozen since 2026-01-01; files signed before the
  Conformance Program (Adobe, OpenAI, cameras) chain to it
  ([contentauth/verify-site](https://github.com/contentauth/verify-site),
  Apache-2.0).

`update-trust-lists.py --latest` fetches newer lists. Your own trust
anchors (a PEM file of CA certificates, for example of your newsroom or
your own test CA) go in the GIMP folder, `content-credentials/trust-anchors.pem`
(for the Flatpak `~/.config/GIMP/3.2/content-credentials/trust-anchors.pem`);
a signer that chains to them is shown as trusted by "your own trust
anchors".

### Which list does what

A signer is trusted only if its certificate chains to the C2PA Trust
List, the Interim Trust List or your own anchors. The TSA list decides only whether a time stamp is trusted (and with it, at
what time the signer's certificate is checked); a signer whose
certificate chains only to a time stamp authority's root is UNKNOWN
SIGNER. When a signer chains to more than one list, the first of C2PA
Trust List, Interim Trust List, your own anchors is named.

To decide this, the plug-in reads the file a second time with only the
signing lists and takes the signer's trust from that read, keeping the
time stamp result of the first read (with the TSA list); when the signer
chains to more than one signing list, it checks them one at a time, in
the order above, so the name shown is always the same. One limit: the
second read either trusts every time stamp or checks them all against the
signing lists, not the TSA list. It trusts them when the first read
trusted every one; when the first read found an untrusted one, it checks
them, so a time stamp trusted only through the TSA list does not count
there, and a signer's certificate that expired after such a time stamp is
reported as expired. That takes a file with time stamps of both kinds
(none of the test or sample files has an untrusted one), and errs toward
less trust, never more.

## Installing

The plug-in is the folder `plug-ins/content-credentials`. It needs the
C2PA library, fetched once (the only step that uses the network):

    plug-ins/content-credentials/fetch-deps.py

Then copy the whole folder, with `vendor/` and `trust/`, into GIMP's
plug-in folder (for the Flatpak and for a native GIMP 3.2 alike,
`~/.config/GIMP/3.2/plug-ins/content-credentials/`) and restart GIMP.

### The C2PA library, and why this one

The validation is done by the reference implementation,
[c2pa-rs](https://github.com/contentauth/c2pa-rs) (Adobe, MIT OR
Apache-2.0), through its Python bindings
[c2pa-python](https://github.com/contentauth/c2pa-python) (MIT OR
Apache-2.0). `fetch-deps.py` downloads the pinned wheel c2pa-python 0.37.12
(with c2pa-rs 0.91.0) from PyPI, checks its SHA-256, and keeps from it
only the three Python files and `libc2pa_c.so`, plus both licence texts.

Checked before choosing (2026-09-27):

- The wheel's `libc2pa_c.so` needs only glibc 2.34, libm and libgcc_s
  (OpenSSL and the rest are linked in), so it loads in the Python 3.13 of
  the GIMP 3.2.6 Flatpak (GNOME 50 runtime, glibc 2.42) without installing
  anything and without network at runtime. The Python part is a ctypes
  wrapper; it imports only the standard library (its declared
  dependencies `cryptography`, `requests` and `toml` are for building and
  signing with Python callbacks, not for reading).
- **c2patool** (the command line tool of c2pa-rs, release binaries for
  Linux x86_64) would work too, as a separate process with JSON output.
  It is not used: it is a second executable (11 MB packed) to ship, make
  executable and run inside the sandbox for each check, and it is built
  from the same c2pa-rs, so it validates no differently; the reader API
  gives the same manifest JSON in process. (Whether its command line
  stayed stable across versions was not checked.)
- **Writing a validator** in Python was not an option: JUMBF, CBOR, COSE
  signatures, X.509 chains, time stamps and the hash bindings of a dozen
  formats is exactly what c2pa-rs is, and the Flatpak's Python has no
  `cryptography` module.
- Least fragile: one pinned, checksummed wheel, loaded in process, with
  the plug-in's own `locate()` (a small parser that finds the manifest
  store in each format) as a cross-check and a fallback: without the
  library the dialog still says that credentials are there and that they
  could not be checked.
- c2pa-python looks for `libc2pa_c.so` in the current folder (and
  `./libs`, `./artifacts`, and a name in `C2PA_LIBRARY_NAME`) before its
  own folder. The plug-in imports it from the folder of its own copy,
  ignores `C2PA_LIBRARY_NAME`, and checks which file was loaded, so that
  a library planted elsewhere is never used (tested).
- Size: `libc2pa_c.so` is 23 MB. Only Linux x86_64 and aarch64 are pinned;
  the wheels for macOS and Windows exist and would work the same way.
- c2pa-rs 0.91 deprecates some settings (the old `trust_anchors`) for
  0.92, planned for mid-November 2026; the plug-in already uses the new
  `trust.anchors` form. Updating means changing the pinned version and
  hashes in `fetch-deps.py` and running the tests.

### Formats

What c2pa-rs reads (its `docs/supported-formats.md`): JPEG, PNG, WebP,
TIFF, DNG, HEIF/HEIC, AVIF, JPEG XL, GIF, SVG, and non-image formats. All
image formats GIMP opens were tested with signed files: JPEG, PNG, WebP,
AVIF, HEIF, JPEG XL, TIFF and GIF. HEIC too, but only without GIMP:
GIMP 3.2.6's HEIF loader crashes on the HEIC sample file of c2pa-rs (an
XMP Toolkit error, then a segmentation fault), with or without
credentials in it.

## Tests

    tests/content-credentials/run.sh           everything
    CC_GUI=0 tests/content-credentials/run.sh  without the dialog tests

- `fetch-images.py` downloads the test files listed in `images.json`,
  each checked against its SHA-256, into `tests/content-credentials/images`
  (not in git): C2PA's public test files (CC BY-SA 4.0: valid, tampered in
  four ways, ingredient-heavy, an expired camera certificate) and test
  files of c2pa-rs (MIT OR Apache-2.0: an Adobe Firefly image edited in
  Photoshop and signed by Adobe, a remote manifest, CAWG identity, and
  unsigned samples of each format). `images/CREDITS.txt` lists them. No
  personal photos.
- `make-fixtures.py` signs test files with the test certificate of c2pa-rs:
  an AI-generated claim in each format, an AI-edited JPEG with two
  ingredients, a PNG tampered after signing, and two files with only XMP
  saying `trainedAlgorithmicMedia`. It also makes a test CA with openssl
  (new keys each run) and a PNG signed by a certificate from it.
- `unit.py` checks the report on every file against `expected.py`, the
  store locator against the library, the library missing, a planted
  library, the offline settings, your own trust anchors, that a CA on a
  TSA list only does not make a signer trusted and that the list named is
  the same in every read, the XMP readers,
  the vocabulary, and that no report and no file of the repository has an
  em or en dash.
- `gimp-test.py` runs the plug-in in GIMP without a window on every file
  GIMP opens, and on an image changed in GIMP, a new image and an XCF; and
  exports test files with GIMP to record what export does to credentials.
- `gui/gui-test.sh` opens the dialog on a Broadway display for six files,
  takes screenshots and closes it.

Everything runs in the GIMP Flatpak isolated from your own folders
(`tests/isolate.sh`, from gimp-devtools), and a listing of your GIMP
folders before and after must be the same.

## Licences

The plug-in: GPL-3.0-or-later. c2pa-python and c2pa-rs (downloaded into
`vendor/`, not in git): MIT OR Apache-2.0, with the Rust crates linked
into `libc2pa_c.so` under their own licences (listed in the SBOM of each
c2pa-rs release). Trust lists: CC BY 4.0 (C2PA) and Apache-2.0 (Adobe).
IPTC Digital Source Type labels and definitions: CC BY 4.0, IPTC.
