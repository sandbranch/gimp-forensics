# Content Credentials for GIMP 3 (work in progress)

A read-only GIMP 3 plug-in that shows the Content Credentials (C2PA
provenance) of the file an image came from: who signed it and when, the
actions and ingredients, whether generative AI is declared (IPTC digital
source type), and whether the credentials are valid, from an unknown
signer, tampered, or missing. Menu **Image > Forensics > Content
Credentials...**.

This folder is laid out as it will sit in the combined gimp-forensics
repository, where it is to be merged:

- `plug-ins/content-credentials/`: the plug-in (`content-credentials.py`,
  the report in `c2pa_report.py`, the trust lists in `trust/`,
  `fetch-deps.py` for the C2PA library)
- `tests/content-credentials/`: `run.sh` runs all tests; `tests/isolate.sh`
  is gimp-plugin-devtools' isolation helper
- `docs/content-credentials.md`: what it shows, what it cannot tell you,
  installing, the library, tests, licences
- `docs/content-credentials-plan.md`: what GIMP's export does to
  credentials (tested), what writing them would take, GIMP issue #8117

Quick start:

    plug-ins/content-credentials/fetch-deps.py
    tests/content-credentials/run.sh

GPL-3.0-or-later (see COPYING). The C2PA library, downloaded into
`vendor/`, is MIT OR Apache-2.0.
