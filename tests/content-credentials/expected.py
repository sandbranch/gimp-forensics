# What the Content Credentials report must say about each test file (the
# files of images.json and those make-fixtures.py makes), for unit.py and
# gimp-test.py.
#
#   status       the report's status
#   container    the start of where the manifest store is (None: no store)
#   manifests    how many manifests the store has
#   codes        validation codes that must be among the problems
#   ai           (level, in an ingredient) pairs that must be found
#   no_ai        no generative AI finding at all
#   ingredients  the ingredient titles of the active manifest, in order
#   depth        how deep the ingredient tree goes (manifests below the
#                active one)
#   xmp_dst      the XMP digital source type term that must be found
#   trusted_by   the trust list that must be named
#   remote       the remote manifest URL must be reported
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later

GEN = 'generated/'

EXPECTED = {
    'adobe-20220124-A.jpg': dict(status='none', container=None, no_ai=True),
    'adobe-20220124-C.jpg': dict(status='untrusted', container='JPEG APP11', manifests=1,
                                 codes=['signingCredential.untrusted'], no_ai=True),
    'adobe-20220124-CAI.jpg': dict(status='untrusted', container='JPEG APP11', manifests=1,
                                   ingredients=['A.jpg', 'I.jpg'], depth=0),
    'adobe-20220124-CACAICAICICA.jpg': dict(status='untrusted', container='JPEG APP11',
                                            manifests=4, ingredients=['CAICA.jpg', 'CICA.jpg'],
                                            depth=2),
    'adobe-20220124-CIE-sig-CA.jpg': dict(status='untrusted', container='JPEG APP11',
                                          manifests=2, codes=['claimSignature.mismatch']),
    'adobe-20220124-E-sig-CA.jpg': dict(status='tampered', container='JPEG APP11',
                                        codes=['claimSignature.mismatch']),
    'adobe-20220124-E-uri-CA.jpg': dict(status='tampered', container='JPEG APP11',
                                        codes=['assertion.hashedURI.mismatch']),
    'adobe-20220124-E-dat-CA.jpg': dict(status='tampered', container='JPEG APP11',
                                        codes=['assertion.dataHash.mismatch']),
    'adobe-20220124-XCA.jpg': dict(status='tampered', container='JPEG APP11',
                                   codes=['assertion.dataHash.mismatch']),
    'adobe-20220124-E-clm-CAICAI.jpg': dict(status='tampered', container='JPEG APP11',
                                            codes=['ingredient.manifest.missing',
                                                   'assertion.hashedURI.mismatch']),
    'nikon-20221019-building.jpeg': dict(status='invalid', container='JPEG APP11',
                                         codes=['signingCredential.expired'], no_ai=True),
    'c2pa-rs-ocsp.jpg': dict(status='trusted', container='JPEG APP11', manifests=2,
                             trusted_by='Interim Trust List',
                             ai=[('edited', False), ('generated', True)],
                             ingredients=['ocsp.jpg'], depth=1),
    'c2pa-rs-cloud.jpg': dict(status='remote', container=None, remote=True),
    'c2pa-rs-C_with_CAWG_data.jpg': dict(status='untrusted', container='JPEG APP11'),
    'c2pa-rs-sample1.png': dict(status='none', container=None),
    'c2pa-rs-sample1.webp': dict(status='none', container=None),
    'c2pa-rs-test.tiff': dict(status='none', container=None),
    GEN + 'ai-generated.png': dict(status='untrusted', container='PNG caBX', manifests=1,
                                   ai=[('generated', False)]),
    GEN + 'test-ca-signed.png': dict(status='untrusted', container='PNG caBX', manifests=1,
                                     codes=['signingCredential.untrusted']),
    GEN + 'ai-generated.webp': dict(status='untrusted', container='WebP C2PA',
                                    ai=[('generated', False)]),
    GEN + 'ai-generated.avif': dict(status='untrusted', container='ISO BMFF uuid',
                                    ai=[('generated', False)]),
    GEN + 'ai-generated.heic': dict(status='untrusted', container='ISO BMFF uuid',
                                    ai=[('generated', False)]),
    GEN + 'ai-generated.heif': dict(status='untrusted', container='ISO BMFF uuid',
                                    ai=[('generated', False)]),
    GEN + 'ai-generated.jxl': dict(status='untrusted', container='JPEG XL jumb',
                                   ai=[('generated', False)]),
    GEN + 'ai-generated.tif': dict(status='untrusted', container='TIFF tag 52545',
                                   ai=[('generated', False)]),
    GEN + 'ai-generated.gif': dict(status='untrusted', container='GIF application',
                                   ai=[('generated', False)]),
    GEN + 'ai-generated.jpg': dict(status='untrusted', container='JPEG APP11',
                                   ai=[('generated', False)]),
    GEN + 'ai-edited.jpg': dict(status='untrusted', container='JPEG APP11', manifests=2,
                                ai=[('edited', False), ('generated', True)],
                                ingredients=['photo.jpg', 'ai-generated.png'], depth=1),
    GEN + 'tampered-pixels.png': dict(status='tampered', container='PNG caBX',
                                      codes=['assertion.dataHash.mismatch']),
    GEN + 'xmp-ai.jpg': dict(status='none', container=None, ai=[('generated', False)],
                             xmp_dst='trainedAlgorithmicMedia'),
    GEN + 'xmp-ai.png': dict(status='none', container=None, ai=[('generated', False)],
                             xmp_dst='trainedAlgorithmicMedia'),
}


def depth(summary):
    """How many levels of manifests are below this one."""
    best = 0
    for ing in summary['ingredients']:
        if ing['manifest']:
            best = max(best, 1 + depth(ing['manifest']))
    return best


def check_report(name, report, exp):
    """A list of what is wrong with the report, empty if it is right."""
    wrong = []
    if report['status'] != exp['status']:
        wrong.append('status %s, expected %s' % (report['status'], exp['status']))
    if 'container' in exp:
        got = (report.get('container') or {}).get('container')
        if exp['container'] is None and got is not None:
            wrong.append('a manifest store found at %s' % got)
        if exp['container'] is not None and not (got or '').startswith(exp['container']):
            wrong.append('manifest store at %s, expected %s' % (got, exp['container']))
    if 'manifests' in exp and report['manifest_count'] != exp['manifests']:
        wrong.append('%d manifests, expected %d' % (report['manifest_count'], exp['manifests']))
    codes = [p['code'] for p in report['problems']]
    for c in exp.get('codes', []):
        if c not in codes:
            wrong.append('no %s among %s' % (c, codes))
    found = {(a['level'], a['in_ingredient']) for a in report['ai']}
    for a in exp.get('ai', []):
        if tuple(a) not in found:
            wrong.append('no generative AI finding %s among %s' % (a, sorted(found)))
    if exp.get('no_ai') and report['ai']:
        wrong.append('generative AI found: %s' % report['ai'])
    if 'ingredients' in exp:
        titles = [i['title'] for i in (report['active'] or {}).get('ingredients', [])]
        if titles != exp['ingredients']:
            wrong.append('ingredients %s, expected %s' % (titles, exp['ingredients']))
    if 'depth' in exp and report['active'] and depth(report['active']) != exp['depth']:
        wrong.append('ingredient tree %d deep, expected %d' % (depth(report['active']),
                                                               exp['depth']))
    if 'xmp_dst' in exp:
        terms = [x['type']['term'] for x in report['xmp']
                 if x['kind'] == 'DigitalSourceType' and x['type']]
        if exp['xmp_dst'] not in terms:
            wrong.append('XMP digital source type %s, expected %s' % (terms, exp['xmp_dst']))
    if 'trusted_by' in exp and not (report.get('trusted_by') or '').startswith(
            exp['trusted_by']):
        wrong.append('trusted by %r, expected %s' % (report.get('trusted_by'),
                                                     exp['trusted_by']))
    if exp.get('remote') and not report.get('remote_url'):
        wrong.append('no remote manifest URL')
    if not report['headline']:
        wrong.append('no headline')
    return wrong
