# Content Credentials: plan

What exists: the read-only dialog (see
[content-credentials.md](content-credentials.md)). What is evaluated here
and not built: keeping or writing credentials when GIMP exports, and what
upstream GIMP might take. Facts checked on 2026-09-27; where something was
not checked, it says so.

## What GIMP does today (tested)

GIMP 3.2.6 (Flatpak) was given files with credentials and asked to export
them (`tests/content-credentials/gimp-test.py`, the `CC EXPORT` lines of
its log):

| Exported from | To | Export defaults | With Exif, XMP and IPTC on |
|---|---|---|---|
| adobe-20220124-C.jpg (test signer) | JPEG, PNG, WebP, TIFF | no credentials, no XMP | no credentials; XMP `dcterms:provenance` kept, pointing at a manifest that is gone |
| c2pa-rs-ocsp.jpg (Firefly + Photoshop, Adobe signed) | JPEG, PNG, WebP, TIFF | no credentials | no credentials; its AI declaration was only in the manifest, so nothing says AI any more |
| ai-generated.png (signed, AI) | JPEG, PNG, WebP, TIFF | no credentials | no credentials |
| xmp-ai.jpg (XMP only, `trainedAlgorithmicMedia`) | JPEG, PNG, WebP, TIFF | no XMP | XMP `DigitalSourceType` kept |

So:

- **GIMP strips Content Credentials on every export**, in all four
  formats, whatever the metadata options. It keeps the unsigned XMP when
  metadata export is on, so a signed AI declaration disappears while an
  unsigned one survives, and a `dcterms:provenance` reference to the lost
  manifest stays behind (the dialog explains that case).
- Through GIMP's non-interactive export procedures the metadata options
  default to off (`include-exif`, `include-xmp`, `include-iptc` are
  `False` in `file-jpeg-export` and `file-png-export`). Not checked: what
  the interactive export dialog of a fresh profile sets them to.
- **Copying the manifest store into the export is worse than dropping
  it**: the store of adobe-20220124-C.jpg put into GIMP's re-encoded
  export validates as TAMPERED (`assertion.dataHash.mismatch`), because
  the hash covers the original bytes (tested). Even without an edit, a
  re-encode changes the bytes. The only correct way to keep provenance is
  a **new manifest** that names the opened file as its `parentOf`
  ingredient (with the old manifest inside) and says what GIMP did.

## Writing credentials on export: what it takes

1. **A new manifest per export**, built by c2pa-rs (`Builder`): the source
   file as `parentOf` ingredient (`c2pa.opened`), files placed as layers as
   `componentOf` (`c2pa.placed`), and the actions GIMP can name. The same
   library the dialog uses does this; `tests/content-credentials/make-fixtures.py`
   already builds such manifests (opened, placed, edited with a digital
   source type, ingredients with and without credentials) and signs JPEG,
   PNG, WebP, AVIF, HEIF, HEIC, JPEG XL, TIFF and GIF.
2. **Actions from GIMP's history.** GIMP has no action log. The undo
   history is per session and lossy; the XCF keeps none of it. A first
   version can declare only what is known for certain: opened, placed
   (layers from other files), resized, cropped, converted, and `c2pa.edited`
   as a catch-all, with GIMP as `softwareAgent`. Declaring actions GIMP
   did not do, or leaving out ones it did, would make the credentials
   lie; `c2pa.edited` without detail is honest. A per-image log of
   filters and tools would be core work in GIMP.
3. **Generative AI.** If a GIMP plug-in or filter uses a generative model
   (GIMP has none built in), its step would need
   `compositeWithTrainedAlgorithmicMedia`. That needs the filter to say
   so; nothing in GIMP's procedure API does today.
4. **Hooking into export.** A plug-in cannot change GIMP's export
   procedures. Options: a separate "Export with Content Credentials..."
   (exports with GIMP's own procedure, then signs the file with
   c2pa-rs: simplest, works now); or, in core, a step after every export
   (upstream only).
5. **A signing certificate** (below). Without a time stamp authority,
   which needs the network at signing time, "signed at" stays empty and
   validity is judged by the certificate's dates alone.

### The certificate

- A **self-signed** or private CA certificate works technically, and the
  result validates as UNKNOWN SIGNER everywhere (as the test files here
  do). It still carries value: the ingredient chain shows the original's
  trusted signature, and anyone who has the user's CA can trust it (the
  plug-in's own `trust-anchors.pem`).
- The **C2PA Trust List**: "Only instances of conforming Generator
  Products are eligible to receive C2PA Claim Signing Certificates from a
  Certification Authority on the C2PA Trust List" (C2PA Conformance
  Program, version 0.2, 2026-07-31, section 2.23). A product has to be
  evaluated against the specification and the Generator Product Security
  Requirements, which assign an Assurance Level, and its maker signs legal
  agreements (conformance-public has a "Generator Product Company
  Agreement"). There is no application fee (section 6.5.1). Assurance
  Levels are conveyed by the CA from "Dynamic Evidence", "usually ... a
  verifiable hardware-backed artifact, such as a key or platform
  attestation report" (section 2.25). Not checked: whether an open source
  desktop program whose keys live on users' computers can meet Assurance
  Level 1, and whether an unincorporated project can sign the agreements.
  Either would need asking conformance@c2pa.org.
- The **CAWG identity assertion** (Creator Assertions Working Group)
  lets a person sign their identity separately from the product's claim
  signature; c2pa-rs supports it and the dialog lists its validation
  codes (`cawg.*`). It does
  not replace the claim signature.

## Upstream: GIMP issue #8117

[#8117](https://gitlab.gnome.org/GNOME/gimp/-/issues/8117), "Support the
C2PA standard for Image Provenance", open since 2022-04-24, labels
"1. Feature" and "5. Metadata". Read in full on 2026-09-27 (three
comments and one cross-reference):

- Jacob Boerema (Wormnest; Developer role in the GNOME/gimp project), 2022-04-24: asked whether it
  is a new metadata format like XMP, IPTC and Exif, and said that then
  "it might be better to open this issue against exiv2, which is the
  metadata handling library we use", and "Without support in exiv2, it
  would need a dedicated volunteer to work on this."
- Tobias (user Tobias, not a member of the project), 2022-04-25: pointed to the exiv2 issue
  ([Exiv2/exiv2#2221](https://github.com/Exiv2/exiv2/issues/2221), "Add
  support for C2PA", open since 2022-04-25, one comment from an exiv2
  collaborator hoping that a contributor finds it interesting; no work
  since), and that C2PA "looks more like a signed container" around XMP,
  IPTC and Exif with a thumbnail.
- 2025-03-15: mentioned in the GSoC 2025 metadata editor discussion
  (#13120, moved to gimp-ux#385), which did not take C2PA up.
- WK (WanradtKoell, not a member of the project), 2026-02-16:
  explained that C2PA is cryptographically signed provenance, not just
  metadata, and that "anyone can verify signature and detect tampering".

No GIMP developer has said what they would accept. What can be read from
the thread and the code, as inference and not as their position:

- The one developer answer points at exiv2 and at a volunteer. exiv2
  has not started; C2PA validation is signatures, certificate chains and
  format-specific hashing, not tag parsing, so it is unlikely to land in
  exiv2 soon. The realistic library is c2pa-rs through its C API
  (`c2pa_c_ffi`, which is what `libc2pa_c.so` is), a Rust dependency GIMP
  does not have; GIMP builds on Windows, macOS, Linux and the Flatpak, so
  that is a build-system decision for the maintainers.
- A **read-only viewer** is the least intrusive offer: no change to
  export, no certificates, no policy. As a plug-in it can be shipped
  outside GIMP now (this one), and offered to the metadata viewer later.
- **Keeping credentials on export** touches every export path and needs a
  signing identity, which is a policy question for the project (would
  GIMP sign as "GIMP", with which key, and on whose computer?). It should
  be proposed in #8117 as a question, with this file's findings, before
  any code.

Suggested next step, when the user agrees: a comment in #8117 with the
test results above (GIMP strips credentials; XMP `dcterms:provenance`
dangles; copying the store makes it tampered), a link to the plug-in, and
the question of whether the maintainers would take a read-only view in
core, and whether c2pa-rs as a dependency is thinkable.

## c2pa-rs: trust list kinds are not kept apart (draft issue, not filed)

Worth reporting: it is a trust decision, not a cosmetic one, and it is
still in c2pa-rs `main` (commit 59b96fa, 2026-09-26: the loop in
`certificate_trust/openssl.rs` and the `HashSet` in `settings/mod.rs` are
unchanged). Related, not the same: #1776 ("Support TSA trust list",
closed 2026-03-16; it added `trust_kind`) and #2039 (separate signer and
TSA lists in c2pa-node's config, open). The plug-in works around it (see
"Which list does what" in [content-credentials.md](content-credentials.md)).
Draft, for contentauth/c2pa-rs:

> **Signer trust accepts TSA (and CAWG) anchors; the trust list reported
> changes from run to run**
>
> c2pa-rs 0.91.0 (c2pa-python 0.37.12). `CertificateTrustPolicy::
> check_certificate_trust` (`sdk/src/crypto/cose/certificate_trust/
> openssl.rs`, and the same loop in `rust_native.rs`) iterates over all
> `anchor_sets()` and returns the first set the certificate chains to,
> whatever its `trust_anchor_type`. It is called for the claim signer
> (`cose/verifier.rs`, `verify_trust`) and for the time stamp authority
> (`time_stamp/verify.rs`) with the same policy. Since #1776 a TSA list
> can be given as `trust.anchors` with `trust_kind: "tsa"`, so:
>
> 1. A claim signer whose certificate chains only to a root in a
>    `trust_kind: "tsa"` set gets `signingCredential.trusted` and the
>    manifest `validation_state` `Trusted`. The C2PA specification keeps
>    the TSA trust list separate from the signer lists (2.3, "Time-stamp
>    authorities"). The reverse holds too: a TSA chaining only to a
>    manifest (signer) root gets `timeStamp.trusted`.
> 2. `Settings::merge_legacy_trust_anchors` (`sdk/src/settings/mod.rs`)
>    collects `trust.anchors` in a `HashSet` and turns it back into a
>    `Vec`, so the order of the sets, and with it the `trust_list_uri`
>    reported in `signingCredential.trusted` and `timeStamp.trusted`,
>    differs between reads when a root is in two sets. Seen on a Google
>    Gemini PNG whose root is on both the C2PA Trust List and the C2PA
>    TSA Trust List: "found in [c2pa-trust-list]" in some reads, "found in
>    [c2pa-tsa-trust-list]" in others, in one process. The per-set
>    `trust_config` (EKU override) applied after a match
>    (`verify_signature`, `get_anchor_set`) then also depends on the order.
>
> To reproduce: make a CA and an end-entity certificate with the
> emailProtection EKU, sign any image with it, then read it with
> `trust.anchors = [{trust_anchors: <CA>, trust_uri: "test-tsa",
> trust_kind: "tsa"}]` and `verify.verify_trust: true`: the result is
> `signingCredential.trusted`, "found in [test-tsa] trust anchors".
>
> Suggested: filter the anchor sets by kind in `check_certificate_trust`
> (a kind parameter: Manifest, plus EndEntity, for signers; TSA for time
> stamps; CAWG for identity), and keep `trust.anchors` in the given order
> (dedup without a `HashSet`, for example by keeping the first of equal
> entries), so that the list reported is deterministic.

## GIMP bugs found (not reported yet)

- GIMP 3.2.6's HEIF loader (`file-heif`) crashes on `sample1.heic` from
  the c2pa-rs test fixtures, with or without credentials: "XMP Toolkit
  error 201: Error in XMLValidator", then a segmentation fault.
  `sample1.heif` opens.
- Exporting with XMP on keeps `dcterms:provenance` although the manifest
  it points to is dropped. Harmless for display, misleading for
  validators; GIMP could drop it along with the manifest.

## Later, for the dialog

- Show the thumbnails stored in the manifest (claim and ingredient
  thumbnails; `Reader.resource_to_stream`).
- Translations (the words live in `c2pa_report.py`).
- Open an ingredient's thumbnail as a new image, for comparison.
- A choice to allow fetching remote manifests, off by default, for files
  like `c2pa-rs-cloud.jpg`.
