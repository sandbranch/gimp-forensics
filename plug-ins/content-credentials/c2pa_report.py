# Content Credentials (C2PA) of an image file, as a plain report: what the
# credentials say (who signed, when, with what, the actions, the digital
# source type, the ingredients) and whether they check out, in plain words.
# Used by content-credentials.py (the GIMP plug-in), and by the tests
# without GIMP. Needs no GIMP and no GTK; the C2PA validation itself is done
# by the c2pa-python bindings of the reference implementation (c2pa-rs),
# which fetch-deps.py puts into vendor/.
#
# Nothing here uses the network: the settings turn off fetching remote
# manifests and OCSP, and allow no network host at all. Trust is decided
# against the trust lists in trust/ (and a file of your own), offline.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later

import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR = os.path.join(HERE, 'vendor')
TRUST_DIR = os.path.join(HERE, 'trust')

# ------------------------------------------------------------ vocabulary

IPTC_DST = 'http://cv.iptc.org/newscodes/digitalsourcetype/'
C2PA_DST = 'http://c2pa.org/digitalsourcetype/'

# The IPTC Digital Source Type vocabulary (cv.iptc.org/newscodes/
# digitalsourcetype, released 2024-10-23), labels and definitions as IPTC
# gives them (CC BY 4.0, IPTC), plus the two C2PA-specific values of the
# C2PA specification. "ai" is how this plug-in flags the term: "generated"
# (the whole image made by generative AI), "edited" (generative AI used on
# part of it), "maybe" (may or may not include generative AI).
DIGITAL_SOURCE_TYPES = {
    'digitalCapture': ('Digital capture sampled from real life',
                       'The media was captured from a real-life source using a digital camera '
                       'or digital recording device', None),
    'computationalCapture': ('Multi-frame computational capture sampled from real life',
                             'The media is the result of capturing multiple frames from a '
                             'real-life source using a digital camera or digital recording '
                             'device, then automatically merging them into a single frame using '
                             'digital signal processing techniques and/or non-generative AI',
                             None),
    'negativeFilm': ('Digitised from a transparent negative',
                     'The media was digitised from a negative on film or other transparent '
                     'medium', None),
    'positiveFilm': ('Digitised from a transparent positive',
                     'The media was digitised from a positive on a transparency or other '
                     'transparent medium', None),
    'print': ('Digitised from a non-transparent medium',
              'The media was digitised from a non-transparent medium such as a photographic '
              'print', None),
    'minorHumanEdits': ('Original media with minor human edits',
                        'Minor augmentation or correction by a human, such as a '
                        'digitally-retouched photo used in a magazine (retired by IPTC)', None),
    'humanEdits': ('Human-edited media',
                   'Augmentation, correction or enhancement by one or more humans using '
                   'non-generative tools', None),
    'compositeWithTrainedAlgorithmicMedia': ('Edited using Generative AI',
                                             'Augmentation, correction or enhancement using a '
                                             'Generative AI model, such as with inpainting or '
                                             'outpainting operations', 'edited'),
    'algorithmicallyEnhanced': ('Algorithmically-altered media',
                                'Modification or correction by algorithm without changing the '
                                'main content of the media, initiated or configured by a human, '
                                'such as sharpening or applying noise reduction', None),
    'softwareImage': ('Created by software',
                      'The digital image was created by computer software (retired by IPTC)',
                      None),
    'digitalArt': ('Digital art', 'Media created by a human using digital tools (retired by '
                   'IPTC)', None),
    'digitalCreation': ('Digital creation', 'Media created by a human using non-generative '
                        'tools', None),
    'dataDrivenMedia': ('Data-driven media', 'Digital media representation of data via human '
                        'programming or creativity', None),
    'trainedAlgorithmicMedia': ('Created using Generative AI',
                                'Digital media created algorithmically using an Artificial '
                                'Intelligence model trained on captured content', 'generated'),
    'algorithmicMedia': ('Pure algorithmic media',
                         'Media created purely by an algorithm not based on any sampled '
                         'training data, e.g. an image created by software using a mathematical '
                         'formula', None),
    'screenCapture': ('Screen capture', 'A capture of the contents of the screen of a computer '
                      'or mobile device', None),
    'virtualRecording': ('Virtual event recording', 'Live recording of virtual event based on '
                         'Generative AI and/or captured elements', 'maybe'),
    'composite': ('Composite of elements', 'Mix or composite of several elements, any of which '
                  'may or may not be generative AI', 'maybe'),
    'compositeCapture': ('Composite of captured elements', 'Mix or composite of several '
                         'elements that are all captures of real life', None),
    'compositeSynthetic': ('Composite including generative AI elements',
                           'Mix or composite of several elements, at least one of which is '
                           'Generative AI', 'edited'),
    # C2PA's own values (C2PA specification, digital source type)
    'empty': ('Empty (C2PA)', 'Media whose digital content is effectively empty, such as a '
              'blank canvas', None),
    'trainedAlgorithmicData': ('Data created using Generative AI (C2PA)',
                               'Data that is the result of algorithmically using a model derived '
                               'from sampled content and data, in a data format rather than a '
                               'media type', 'generated'),
}

AI_WORDS = {
    'generated': 'Generative AI: made by generative AI',
    'edited': 'Generative AI: edited or composited with generative AI',
    'maybe': 'Generative AI: may include generative AI',
}


def source_type(uri):
    """(term, label, definition, ai) for a digital source type URI or term;
    an unknown one keeps its URI as the label."""
    if not uri:
        return None
    uri = str(uri).strip()
    term = uri.rstrip('/').rsplit('/', 1)[-1]
    if term in DIGITAL_SOURCE_TYPES:
        label, definition, ai = DIGITAL_SOURCE_TYPES[term]
        return {'uri': uri, 'term': term, 'label': label, 'definition': definition, 'ai': ai}
    return {'uri': uri, 'term': term, 'label': uri, 'definition': 'not an IPTC or C2PA term '
            'known to this plug-in', 'ai': None}


# The actions of the C2PA specification (2.x), in plain words
ACTIONS = {
    'c2pa.created': 'Created',
    'c2pa.opened': 'Opened an existing file',
    'c2pa.placed': 'Placed another file into it',
    'c2pa.edited': 'Edited',
    'c2pa.edited.metadata': 'Edited the metadata',
    'c2pa.color_adjustments': 'Adjusted colours',
    'c2pa.converted': 'Changed the file format',
    'c2pa.cropped': 'Cropped',
    'c2pa.drawing': 'Drew or painted',
    'c2pa.filtered': 'Applied filters',
    'c2pa.orientation': 'Rotated or flipped',
    'c2pa.removed': 'Removed a placed file',
    'c2pa.redacted': 'Redacted',
    'c2pa.published': 'Published',
    'c2pa.repackaged': 'Repackaged (container changed)',
    'c2pa.resized': 'Resized',
    'c2pa.transcoded': 'Transcoded',
    'c2pa.translated': 'Translated',
    'c2pa.watermarked': 'Watermarked',
    'c2pa.watermarked.bound': 'Watermarked (soft binding)',
    'c2pa.watermarked.unbound': 'Watermarked',
    'c2pa.unknown': 'Something the software could not name',
}

RELATIONSHIPS = {
    'parentOf': 'the file this one was made from',
    'componentOf': 'a file placed into this one',
    'inputTo': 'an input (for example a prompt image) to a tool',
}

# validation status codes (C2PA specification, as reported by c2pa-rs), in
# groups, with plain words
CONTENT_CHANGED = {
    'assertion.dataHash.mismatch', 'assertion.bmffHash.mismatch',
    'assertion.boxesHash.mismatch', 'assertion.collectionHash.mismatch',
}
CREDENTIALS_ALTERED = {
    'claimSignature.mismatch', 'assertion.hashedURI.mismatch', 'hashedURI.mismatch',
    'ingredient.hashedURI.mismatch', 'assertion.missing', 'assertion.undeclared',
    'claim.malformed', 'claim.missing', 'claim.multiple', 'claim.cbor.invalid',
    'claimSignature.missing', 'assertion.cbor.invalid', 'assertion.json.invalid',
    'claim.hardBindings.missing', 'assertion.multipleHardBindings',
    'assertion.dataHash.malformed', 'assertion.bmffHash.malformed',
    'assertion.boxesHash.malformed', 'assertion.boxesHash.unknownBox',
    'manifest.compressed.invalid', 'assertion.action.ingredientMismatch',
    'assertion.action.malformed', 'assertion.ingredient.malformed', 'hashedURI.missing',
    'assertion.notRedacted', 'assertion.selfRedacted', 'assertion.outsideManifest',
}
CERTIFICATE = {
    'signingCredential.expired', 'signingCredential.invalid', 'signingCredential.ocsp.revoked',
    'claimSignature.outsideValidity', 'timeStamp.mismatch', 'timeStamp.malformed',
    'timeStamp.outsideValidity', 'algorithm.unsupported', 'signingCredential.ocsp.unknown',
}
PLAIN = {
    'signingCredential.untrusted': 'The signer\'s certificate is not on a trust list this '
                                   'plug-in knows',
    'signingCredential.trusted': 'The signer\'s certificate is on a trust list',
    'signingCredential.expired': 'The signer\'s certificate had expired when it signed',
    'signingCredential.invalid': 'The signer\'s certificate is not valid',
    'signingCredential.ocsp.revoked': 'The signer\'s certificate was revoked',
    'claimSignature.mismatch': 'The signature does not match the credentials: they were '
                               'changed after signing',
    'claimSignature.outsideValidity': 'The signature was made outside the certificate\'s '
                                      'validity period',
    'assertion.dataHash.mismatch': 'The image data in the file is not what was signed: the '
                                   'file was changed after signing',
    'assertion.bmffHash.mismatch': 'The media data in the file is not what was signed: the '
                                   'file was changed after signing',
    'assertion.boxesHash.mismatch': 'Parts of the file are not what was signed: the file was '
                                    'changed after signing',
    'assertion.collectionHash.mismatch': 'The files of the collection are not what was signed',
    'assertion.hashedURI.mismatch': 'A statement in the credentials was changed after signing',
    'hashedURI.mismatch': 'A reference in the credentials was changed after signing',
    'ingredient.hashedURI.mismatch': 'An ingredient reference was changed after signing',
    'ingredient.manifest.missing': 'The credentials of an ingredient are missing',
    'ingredient.manifest.mismatch': 'The credentials of an ingredient do not match',
    'ingredient.claimSignature.mismatch': 'The signature of an ingredient does not match',
    'assertion.missing': 'A statement the credentials refer to is missing',
    'timeStamp.untrusted': 'The time stamp authority is not on a trust list this plug-in knows',
    'timeStamp.mismatch': 'The time stamp does not match the signature',
    'timeStamp.outsideValidity': 'The time stamp is outside the certificate\'s validity',
    'manifest.inaccessible': 'The credentials could not be read',
    'general.error': 'The validator reported an error',
}


def plain_code(code, explanation=''):
    if code in PLAIN:
        return PLAIN[code]
    return explanation or code


# ------------------------------------------------------------ the library

_c2pa = None
_c2pa_error = None


def load_c2pa():
    """The c2pa module from vendor/ (or the Python path), or None; the
    error is in library_error()."""
    global _c2pa, _c2pa_error
    if _c2pa is not None or _c2pa_error is not None:
        return _c2pa
    if os.path.isdir(VENDOR) and VENDOR not in sys.path:
        sys.path.insert(0, VENDOR)
    # c2pa-python looks for libc2pa_c.so in the current folder first (and
    # in ./libs, ./artifacts, and a name from C2PA_LIBRARY_NAME), before its
    # own folder: so that no other copy is loaded, the import runs in the
    # folder of our copy, and the loaded file is checked.
    libs = os.path.join(VENDOR, 'c2pa', 'libs')
    cwd = os.getcwd() if os.path.isdir(libs) else None
    os.environ.pop('C2PA_LIBRARY_NAME', None)
    try:
        if cwd is not None:
            os.chdir(libs)
        try:
            import c2pa  # noqa: WPS433 (loaded late on purpose)
        finally:
            if cwd is not None:
                os.chdir(cwd)
        loaded = getattr(getattr(sys.modules.get('c2pa.c2pa'), '_lib', None), '_name', '')
        if cwd is not None and os.path.realpath(loaded) != os.path.realpath(
                os.path.join(libs, 'libc2pa_c.so')):
            raise ImportError('loaded %s instead of the copy in %s' % (loaded, libs))
        c2pa.sdk_version()
        _c2pa = c2pa
    except Exception as e:  # ImportError, OSError or RuntimeError from ctypes
        _c2pa_error = '%s: %s' % (type(e).__name__, e)
    return _c2pa


def library_error():
    return _c2pa_error


def library_version():
    c2pa = load_c2pa()
    if c2pa is None:
        return None
    try:
        return 'c2pa-rs %s' % c2pa.sdk_version()
    except Exception:
        return 'c2pa-rs (version unknown)'


# ------------------------------------------------------------ trust lists

def _read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def _count_certs(pem):
    return pem.count('-----BEGIN CERTIFICATE-----')


def trust_lists(user_file=None):
    """The trust lists used: the official C2PA Trust List, the frozen
    Interim Trust List, the C2PA TSA list, and the user's own PEM file if
    there is one. A list of dicts: name, uri, kind, file, certs, pem, and
    allowed/config for the Interim list."""
    lists = []

    def add(name, uri, kind, filename, allowed=None, config=None):
        path = os.path.join(TRUST_DIR, filename)
        if not os.path.isfile(path):
            return
        entry = {'name': name, 'uri': uri, 'kind': kind, 'file': path, 'pem': _read(path)}
        entry['certs'] = _count_certs(entry['pem'])
        if allowed and os.path.isfile(os.path.join(TRUST_DIR, allowed)):
            entry['allowed'] = _read(os.path.join(TRUST_DIR, allowed))
            entry['certs'] += _count_certs(entry['allowed'])
        if config and os.path.isfile(os.path.join(TRUST_DIR, config)):
            entry['config'] = _read(os.path.join(TRUST_DIR, config))
        lists.append(entry)

    add('C2PA Trust List', 'c2pa-trust-list', 'manifest', 'C2PA-TRUST-LIST.pem')
    add('Interim Trust List (frozen 2026-01-01)', 'interim-trust-list', 'manifest',
        'itl-anchors.pem', allowed='itl-allowed.pem', config='itl-store.cfg')
    add('C2PA TSA Trust List', 'c2pa-tsa-trust-list', 'tsa', 'C2PA-TSA-TRUST-LIST.pem')
    if user_file and os.path.isfile(user_file):
        pem = _read(user_file)
        if _count_certs(pem):
            lists.append({'name': 'your own trust anchors', 'uri': 'user-trust-anchors',
                          'kind': 'manifest', 'file': user_file, 'pem': pem,
                          'certs': _count_certs(pem)})
    return lists


def settings(lists):
    """c2pa-rs settings: validate, offline, with the given trust lists."""
    anchors = []
    for t in lists:
        a = {'trust_anchors': t['pem'], 'trust_uri': t['uri'], 'trust_kind': t['kind']}
        if 'allowed' in t:
            a['allowed_list'] = t['allowed']
        if 'config' in t:
            a['trust_config'] = t['config']
        anchors.append(a)
    s = {
        'version': 1,
        'verify': {
            'verify_after_reading': True,
            'verify_trust': True,
            'verify_timestamp_trust': True,
            'ocsp_fetch': False,
            'remote_manifest_fetch': False,
        },
        # no network host at all (None would mean any)
        'core': {'allowed_network_hosts': []},
    }
    if anchors:
        s['trust'] = {'anchors': anchors}
    return s


# ------------------------------------------------------------ where in the file

C2PA_BMFF_UUID = bytes([0xd8, 0xfe, 0xc3, 0xd6, 0x1b, 0x0e, 0x48, 0x3c,
                        0x92, 0x97, 0x58, 0x28, 0x87, 0x7e, 0xc4, 0x81])


def _jpeg_segments(data):
    """(marker, payload offset, payload length) of the JPEG header segments,
    up to the start of scan."""
    if data[:2] != b'\xff\xd8':
        return
    i = 2
    n = len(data)
    while i + 4 <= n:
        if data[i] != 0xff:
            return
        marker = data[i + 1]
        if marker == 0xff:
            i += 1
            continue
        if marker in (0xd8, 0x01) or 0xd0 <= marker <= 0xd7:
            i += 2
            continue
        if marker == 0xd9:
            return
        length = struct.unpack('>H', data[i + 2:i + 4])[0]
        if length < 2:
            return
        yield marker, i + 4, length - 2
        if marker == 0xda:
            return
        i += 2 + length


def _png_chunks(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return
    i = 8
    while i + 8 <= len(data):
        length, ctype = struct.unpack('>I4s', data[i:i + 8])
        yield ctype, i + 8, length
        if ctype == b'IEND':
            return
        i += 12 + length


def _riff_chunks(data):
    if data[:4] != b'RIFF' or data[8:12] != b'WEBP':
        return
    i = 12
    while i + 8 <= len(data):
        ctype, length = struct.unpack('<4sI', data[i:i + 8])
        yield ctype, i + 8, length
        i += 8 + length + (length & 1)


def _bmff_boxes(data, start=0, end=None, depth=0):
    end = len(data) if end is None else end
    i = start
    while i + 8 <= end:
        size, btype = struct.unpack('>I4s', data[i:i + 8])
        header = 8
        if size == 1 and i + 16 <= end:
            size = struct.unpack('>Q', data[i + 8:i + 16])[0]
            header = 16
        elif size == 0:
            size = end - i
        if size < header:
            return
        yield btype, i + header, size - header, depth
        if btype in (b'meta', b'moov', b'trak', b'iinf') and depth < 3:
            # (meta is a full box: 4 bytes of version and flags)
            skip = 4 if btype == b'meta' else 0
            yield from _bmff_boxes(data, i + header + skip, i + size, depth + 1)
        i += size


def _tiff_tags(data):
    """(tag, type, count, value offset) of all IFDs of a (classic) TIFF."""
    if data[:4] == b'II*\x00':
        e = '<'
    elif data[:4] == b'MM\x00*':
        e = '>'
    else:
        return
    offset = struct.unpack(e + 'I', data[4:8])[0]
    seen = set()
    while offset and offset + 2 <= len(data) and offset not in seen and len(seen) < 64:
        seen.add(offset)
        count = struct.unpack(e + 'H', data[offset:offset + 2])[0]
        for k in range(count):
            p = offset + 2 + 12 * k
            if p + 12 > len(data):
                return
            tag, typ, cnt = struct.unpack(e + 'HHI', data[p:p + 8])
            val = struct.unpack(e + 'I', data[p + 8:p + 12])[0]
            yield tag, typ, cnt, val
        p = offset + 2 + 12 * count
        if p + 4 > len(data):
            return
        offset = struct.unpack(e + 'I', data[p:p + 4])[0]


def locate(path):
    """Where the file keeps a C2PA manifest store, found without the
    library: {'container': ..., 'bytes': n} or None. Also a check on the
    library, and a fallback when it is missing."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    # JPEG: APP11 segments with a JUMBF box labelled c2pa
    total = 0
    segs = 0
    for marker, pos, length in _jpeg_segments(data):
        if marker == 0xeb and data[pos:pos + 2] == b'JP' and (
                b'c2pa' in data[pos:pos + min(length, 64)] or segs):
            segs += 1
            total += length
    if segs:
        return {'container': 'JPEG APP11 segments (JUMBF), %d segment%s' %
                (segs, '' if segs == 1 else 's'), 'bytes': total}
    for ctype, pos, length in _png_chunks(data):
        if ctype == b'caBX':
            return {'container': 'PNG caBX chunk (JUMBF)', 'bytes': length}
    for ctype, pos, length in _riff_chunks(data):
        if ctype == b'C2PA':
            return {'container': 'WebP C2PA chunk (JUMBF)', 'bytes': length}
    if data[4:8] == b'ftyp' or data[:12] == b'\x00\x00\x00\x0cJXL \r\n\x87\n':
        brand = data[8:12].decode('latin-1')
        for btype, pos, length, depth in _bmff_boxes(data):
            if btype == b'uuid' and data[pos:pos + 16] == C2PA_BMFF_UUID:
                return {'container': 'ISO BMFF uuid box (JUMBF), brand %s' % brand.strip(),
                        'bytes': length}
            if btype == b'jumb' and b'c2pa' in data[pos:pos + 64]:
                return {'container': 'JPEG XL jumb box (JUMBF)', 'bytes': length}
    for tag, typ, cnt, val in _tiff_tags(data):
        if tag == 0xcd41:
            return {'container': 'TIFF tag 52545 (C2PA, JUMBF)', 'bytes': cnt}
    if data[:4] == b'GIF8':
        i = data.find(b'\x21\xff\x0bC2PA_GIF')
        if i >= 0:
            return {'container': 'GIF application extension C2PA_GIF', 'bytes': None}
    return None


# ------------------------------------------------------------ XMP in the file

XMP_NS = {
    'Iptc4xmpExt': 'http://iptc.org/std/Iptc4xmpExt/2008-02-29/',
    'dcterms': 'http://purl.org/dc/terms/',
    'xmp': 'http://ns.adobe.com/xap/1.0/',
    'rdf': 'http://www.w3.org/1999/02/22-rdf-syntax-ns#',
}


def xmp_packet(path):
    """The XMP packet of the file itself (JPEG APP1, PNG iTXt, WebP XMP
    chunk, TIFF tag 700), not one inside an embedded thumbnail; or None."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    head = b'http://ns.adobe.com/xap/1.0/\x00'
    for marker, pos, length in _jpeg_segments(data):
        if marker == 0xe1 and data[pos:pos + len(head)] == head:
            return data[pos + len(head):pos + length].decode('utf-8', 'replace')
    for ctype, pos, length in _png_chunks(data):
        if ctype == b'iTXt' and data[pos:pos + 18] == b'XML:com.adobe.xmp\x00':
            body = data[pos + 18:pos + length]
            # compression flag, method, language\0, translated keyword\0
            if len(body) > 2 and body[0] == 0:
                rest = body[2:]
                rest = rest[rest.find(b'\x00') + 1:]
                rest = rest[rest.find(b'\x00') + 1:]
                return rest.decode('utf-8', 'replace')
    for ctype, pos, length in _riff_chunks(data):
        if ctype == b'XMP ':
            return data[pos:pos + length].decode('utf-8', 'replace')
    for tag, typ, cnt, val in _tiff_tags(data):
        if tag == 700 and val + cnt <= len(data):
            return data[val:val + cnt].decode('utf-8', 'replace')
    return None


def xmp_values(packet):
    """{'DigitalSourceType': [...], 'provenance': [...], 'CreatorTool':
    [...]} from an XMP packet, as attributes or elements."""
    found = {'DigitalSourceType': [], 'provenance': [], 'CreatorTool': []}
    if not packet:
        return found
    m = re.search(r'<x:xmpmeta.*?</x:xmpmeta>', packet, re.S)
    text = m.group(0) if m else packet
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        return found
    want = {
        '{%s}DigitalSourceType' % XMP_NS['Iptc4xmpExt']: 'DigitalSourceType',
        '{%s}provenance' % XMP_NS['dcterms']: 'provenance',
        '{%s}CreatorTool' % XMP_NS['xmp']: 'CreatorTool',
    }
    for el in root.iter():
        for key, value in el.attrib.items():
            if key in want and value.strip():
                found[want[key]].append(value.strip())
        if el.tag in want:
            value = (el.text or '').strip()
            if not value:
                # rdf:resource, or an rdf:Alt/Seq with rdf:li
                value = el.attrib.get('{%s}resource' % XMP_NS['rdf'], '')
                for li in el.iter('{%s}li' % XMP_NS['rdf']):
                    if (li.text or '').strip():
                        value = li.text.strip()
                        break
            if value:
                found[want[el.tag]].append(value)
    return found


# ------------------------------------------------------------ the report

def _software(agent):
    if isinstance(agent, dict):
        name = agent.get('name', '')
        version = agent.get('version')
        return '%s %s' % (name, version) if version else name
    return str(agent) if agent else ''


def _claim_generator(m):
    info = m.get('claim_generator_info') or []
    names = []
    for g in info:
        if isinstance(g, dict) and g.get('name'):
            names.append(_software(g))
    if names:
        return ', '.join(names)
    return m.get('claim_generator', '')


def _actions(m):
    out = []
    for a in m.get('assertions') or []:
        label = a.get('label', '')
        if not re.match(r'^c2pa\.actions(\.v\d+)?(__\d+)?$', label):
            continue
        for act in (a.get('data') or {}).get('actions') or []:
            name = act.get('action', '')
            params = act.get('parameters') or {}
            dst = source_type(act.get('digitalSourceType') or params.get('digitalSourceType'))
            agent = act.get('softwareAgent')
            if agent is None and isinstance(act.get('softwareAgentIndex'), int):
                info = m.get('claim_generator_info') or []
                if act['softwareAgentIndex'] < len(info):
                    agent = info[act['softwareAgentIndex']]
            out.append({
                'action': name,
                'label': ACTIONS.get(name, name),
                'when': act.get('when', ''),
                'software_agent': _software(agent),
                'description': act.get('description') or params.get('description', ''),
                'digital_source_type': dst,
            })
    return out


def _authors(m):
    names = []
    for a in m.get('assertions') or []:
        if a.get('label', '').startswith('stds.schema-org.CreativeWork'):
            authors = (a.get('data') or {}).get('author') or []
            if isinstance(authors, dict):
                authors = [authors]
            for p in authors:
                if isinstance(p, dict) and p.get('name') and p['name'] not in names:
                    names.append(p['name'])
    return names


def _codes(entries):
    return [{'code': e.get('code', ''), 'explanation': e.get('explanation', ''),
             'plain': plain_code(e.get('code', ''), e.get('explanation', '')),
             'url': e.get('url', '')} for e in entries or []]


def _manifest(store, label, seen, deltas=None):
    """A summary of the manifest with this label, with its ingredients and
    their manifests below it. deltas: validation results of ingredients,
    by the URI of their ingredient assertion."""
    m = store.get('manifests', {}).get(label)
    if m is None:
        return None
    deltas = deltas or {}
    sig = m.get('signature_info') or {}
    summary = {
        'label': label,
        'title': m.get('title', ''),
        'format': m.get('format', ''),
        'claim_generator': _claim_generator(m),
        'signer': {
            'issuer': sig.get('issuer', ''),
            'common_name': sig.get('common_name', ''),
            'alg': sig.get('alg', ''),
            'time': sig.get('time', ''),
        },
        'authors': _authors(m),
        'actions': _actions(m),
        'ingredients': [],
    }
    seen = seen | {label}
    for ing in m.get('ingredients') or []:
        child = ing.get('active_manifest')
        uri = 'self#jumbf=/c2pa/%s/c2pa.assertions/%s' % (label, ing.get('label', ''))
        delta = deltas.get(uri) or {}
        # what was recorded when the ingredient was used, and what the
        # validator finds now
        found = _codes(ing.get('validation_status'))
        found += _codes((ing.get('validation_results') or {}).get('activeManifest', {})
                        .get('failure'))
        found += _codes(delta.get('failure'))
        problems = []
        trust = None
        for c in found:
            if c['code'] == 'signingCredential.untrusted':
                trust = trust or 'untrusted'
            elif c['code'] not in [p['code'] for p in problems]:
                problems.append(c)
        for c in _codes(delta.get('success')):
            if c['code'] == 'signingCredential.trusted':
                trust = 'trusted'
        entry = {
            'title': ing.get('title', ''),
            'format': ing.get('format', ''),
            'relationship': ing.get('relationship', ''),
            'relationship_label': RELATIONSHIPS.get(ing.get('relationship', ''),
                                                    ing.get('relationship', '')),
            'problems': problems,
            'trust': trust if child else None,
            'manifest': None,
            'has_credentials': bool(child),
            'manifest_missing': False,
        }
        if child and child not in seen:
            entry['manifest'] = _manifest(store, child, seen, deltas)
            entry['manifest_missing'] = entry['manifest'] is None
        summary['ingredients'].append(entry)
    return summary


def _walk(summary, path=()):
    """(manifest summary, path of ingredient titles) of the manifest and all
    ingredient manifests below it."""
    if summary is None:
        return
    yield summary, path
    for ing in summary['ingredients']:
        yield from _walk(ing['manifest'], path + (ing['title'] or '(no title)',))


STATUS_WORDS = {
    'trusted': 'Valid Content Credentials from a trusted signer',
    'untrusted': 'Valid Content Credentials, but the signer is not on a trust list',
    'tampered': 'Tampered: the file or its credentials changed after signing',
    'invalid': 'Invalid Content Credentials',
    'none': 'No Content Credentials found',
    'remote': 'Content Credentials are stored online (not fetched)',
    'unreadable': 'Content Credentials could not be read',
    'unavailable': 'Content Credentials not checked (the C2PA library is missing)',
    'unsupported': 'Content Credentials not checked (a file type C2PA does not cover)',
    'nofile': 'No file to check',
}

HONEST = [
    'Content Credentials record who signed a file and what they declared about it. '
    'They do not prove that what the image shows is true.',
    'Missing credentials prove nothing: most software, GIMP included, drops them when it '
    'saves a file, and most images never had any.',
]


def _classify(state, failures):
    codes = {f['code'] for f in failures}
    if codes & CONTENT_CHANGED or codes & CREDENTIALS_ALTERED:
        return 'tampered'
    if state == 'Trusted':
        return 'trusted'
    if state == 'Valid':
        return 'untrusted'
    if state == 'Invalid' or codes - {'signingCredential.untrusted'}:
        return 'invalid'
    return 'untrusted'


def analyze(path, user_trust_file=None, gimp_xmp=None, gimp_xmp_source=None):
    """The report for the file at path, a dict (JSON-serialisable). gimp_xmp
    is the XMP GIMP read (a dict like xmp_values() returns, or None);
    gimp_xmp_source says where that came from."""
    report = {
        'file': path or '',
        'library': None,
        'trust_lists': [],
        'status': 'nofile',
        'headline': '',
        'explanation': [],
        'problems': [],
        'container': None,
        'active': None,
        'manifest_count': 0,
        'ai': [],
        'xmp': [],
        'remote_url': None,
        'error': None,
        'honest': HONEST,
    }
    if not path or not os.path.isfile(path):
        report['headline'] = STATUS_WORDS['nofile']
        report['explanation'].append('The image has no file on disk (it is new, or the file '
                                     'is gone). Content Credentials are checked in the file.')
        _add_xmp(report, gimp_xmp, gimp_xmp_source, None)
        return report
    st = os.stat(path)
    report['file_size'] = st.st_size
    report['file_mtime'] = st.st_mtime
    report['container'] = locate(path)
    lists = trust_lists(user_trust_file)
    report['trust_lists'] = [{'name': t['name'], 'file': t['file'], 'certs': t['certs'],
                              'kind': t['kind']} for t in lists]

    c2pa = load_c2pa()
    if c2pa is None:
        report['library'] = {'available': False, 'error': library_error()}
        report['status'] = 'unavailable'
        report['explanation'].append('The C2PA library (c2pa-python) is not installed next to '
                                     'the plug-in: run fetch-deps.py. (%s)' % library_error())
        if report['container']:
            report['explanation'].append('The file does contain a C2PA manifest store (%s), '
                                         'but it cannot be validated without the library.'
                                         % report['container']['container'])
        _finish(report, gimp_xmp, gimp_xmp_source, path)
        return report
    report['library'] = {'available': True, 'version': library_version()}

    store = None
    try:
        ctx = c2pa.Context.from_dict(settings(lists))
        reader = c2pa.Reader.try_create(path, context=ctx)
        if reader is not None:
            store = json.loads(reader.json())
            try:
                if not reader.is_embedded():
                    report['remote_url'] = reader.get_remote_url()
            except Exception:
                pass
            reader.close()
    except Exception as e:
        name = type(e).__name__
        text = str(e)
        m = re.search(r'(https?://\S+)', text)
        if 'Remote' in name or text.startswith('Remote'):
            report['status'] = 'remote'
            report['remote_url'] = m.group(1) if m else None
            report['explanation'].append(
                'The file points to Content Credentials kept on a server%s. This plug-in does '
                'not go online, so they were not fetched or checked.'
                % (' (%s)' % report['remote_url'] if report['remote_url'] else ''))
        elif 'NotSupported' in name or text.startswith('NotSupported'):
            report['status'] = 'unsupported'
            report['explanation'].append('The C2PA library does not read this file type, so '
                                         'it was not checked. (%s)' % text)
        else:
            report['status'] = 'unreadable'
            report['error'] = '%s: %s' % (name, text)
            report['explanation'].append('The validator could not read the credentials: %s'
                                         % text)
        _finish(report, gimp_xmp, gimp_xmp_source, path)
        return report

    if store is None:
        report['status'] = 'none'
        report['explanation'].append('The file has no C2PA manifest store.')
        _finish(report, gimp_xmp, gimp_xmp_source, path)
        return report

    report['manifest_count'] = len(store.get('manifests') or {})
    active_label = store.get('active_manifest')
    results = store.get('validation_results') or {}
    deltas = {d.get('ingredientAssertionURI'): d.get('validationDeltas') or {}
              for d in results.get('ingredientDeltas') or []}
    report['active'] = _manifest(store, active_label, frozenset(), deltas)
    state = store.get('validation_state', '')
    vr = results.get('activeManifest') or {}
    failures = _codes(vr.get('failure'))
    if not vr:
        failures = _codes([s for s in store.get('validation_status') or []
                           if s.get('url', '').find(str(active_label)) >= 0])
    successes = _codes(vr.get('success'))
    report['validation_state'] = state
    report['status'] = _classify(state, failures)
    report['problems'] = failures
    # problems of ingredients (found now, or recorded when they were used)
    # do not make this file's own credentials invalid, but are shown
    bad = []
    for m, where in _walk(report['active']):
        for ing in m['ingredients']:
            name = ' > '.join(where + (ing['title'] or '(no title)',))
            for c in ing['problems']:
                c = dict(c, plain='In ingredient %s: %s' % (name, c['plain']))
                report['problems'].append(c)
                if name not in bad:
                    bad.append(name)
            if ing['manifest_missing'] and name not in bad:
                bad.append(name)
    trusted_by = [s['explanation'] for s in successes if s['code'] == 'signingCredential.trusted']
    if report['status'] == 'trusted':
        which = ''
        for t in lists:
            if trusted_by and '[%s]' % t['uri'] in trusted_by[0]:
                which = t['name']
        report['trusted_by'] = which
        report['explanation'].append('The signature is valid and the signer\'s certificate '
                                     'chains to %s.' % (which or 'a trust list'))
    elif report['status'] == 'untrusted':
        report['explanation'].append('The signature is valid and the file is unchanged since '
                                     'signing, but the signer\'s certificate is not on any '
                                     'trust list this plug-in has, so anyone could have made '
                                     'it (test certificates are like this).')
    elif report['status'] == 'tampered':
        codes = {f['code'] for f in failures}
        if codes & CONTENT_CHANGED:
            report['explanation'].append('The file on disk is not what was signed: its image '
                                         'data was changed after the credentials were made.')
        if codes & CREDENTIALS_ALTERED:
            report['explanation'].append('The credentials themselves were changed after '
                                         'signing.')
    else:
        report['explanation'].append('The validator found problems with the credentials '
                                     '(see below).')
    if bad:
        report['explanation'].append('The credentials of %s %s have problems (see below): '
                                     'this file was made from something whose own credentials '
                                     'did not check out.' % (
                                         'ingredient' if len(bad) == 1 else 'ingredients',
                                         ', '.join(bad)))
    _finish(report, gimp_xmp, gimp_xmp_source, path)
    return report


def _add_xmp(report, gimp_xmp, gimp_xmp_source, path):
    """XMP findings: from GIMP's metadata, else from the file itself."""
    sources = []
    if gimp_xmp:
        sources.append((gimp_xmp, gimp_xmp_source or 'XMP metadata of the image in GIMP'))
    if path and not (gimp_xmp and any(gimp_xmp.values())):
        packet = xmp_packet(path)
        if packet:
            sources.append((xmp_values(packet), 'XMP metadata in the file'))
    for values, where in sources:
        for uri in values.get('DigitalSourceType') or []:
            dst = source_type(uri)
            report['xmp'].append({'kind': 'DigitalSourceType', 'value': uri, 'type': dst,
                                  'source': where})
        for uri in values.get('provenance') or []:
            report['xmp'].append({'kind': 'provenance', 'value': uri, 'type': None,
                                  'source': where})
        for tool in values.get('CreatorTool') or []:
            report['xmp'].append({'kind': 'CreatorTool', 'value': tool, 'type': None,
                                  'source': where})
        if values and any(values.values()):
            break


def _finish(report, gimp_xmp, gimp_xmp_source, path):
    _add_xmp(report, gimp_xmp, gimp_xmp_source, path)
    # generative AI, from the credentials and from XMP
    active = report.get('active')
    for m, where in _walk(active):
        signed = m['signer']['common_name'] or m['signer']['issuer'] or 'an unknown signer'
        for act in m['actions']:
            dst = act['digital_source_type']
            if dst and dst['ai']:
                report['ai'].append({
                    'level': dst['ai'],
                    'type': dst,
                    'action': act['action'],
                    'software_agent': act['software_agent'],
                    'source': 'C2PA manifest%s, action %s, signed by %s' % (
                        '' if not where else ' of ingredient ' + ' > '.join(where),
                        act['action'], signed),
                    'in_ingredient': bool(where),
                })
    for x in report['xmp']:
        if x['kind'] == 'DigitalSourceType' and x['type'] and x['type']['ai']:
            report['ai'].append({'level': x['type']['ai'], 'type': x['type'], 'action': None,
                                 'software_agent': '', 'source': x['source'] + ' (not signed: '
                                 'anyone can write or remove it)', 'in_ingredient': False})
    if report['status'] in ('none', 'nofile') and any(x['kind'] == 'provenance'
                                                     for x in report['xmp']):
        report['explanation'].append('The XMP metadata refers to Content Credentials '
                                     '(dcterms:provenance), but the file has none: they were '
                                     'probably removed when the file was saved by software '
                                     'that does not keep them.')
    report['headline'] = STATUS_WORDS[report['status']]


def ai_summary(report):
    """One line on generative AI, or None: the strongest finding for the
    image itself, then for its ingredients."""
    parts = []
    for inside in (False, True):
        found = [a for a in report['ai'] if a['in_ingredient'] == inside]
        for level in ('generated', 'edited', 'maybe'):
            hits = [a for a in found if a['level'] == level]
            if hits:
                a = hits[0]
                parts.append('%s%s (IPTC %s: "%s")%s' % (
                    'an ingredient: ' if inside else '', AI_WORDS[level].split(': ', 1)[1],
                    a['type']['term'], a['type']['label'],
                    ', says the XMP metadata, which is not signed' if a['action'] is None
                    else ''))
                break
    if not parts:
        return None
    return 'Generative AI: ' + '; '.join(parts)


# ------------------------------------------------------------ as text

def ingredient_note(ing):
    """', no credentials' and the like, for an ingredient."""
    if not ing['has_credentials']:
        return ', no credentials'
    if ing['manifest_missing']:
        return ', its credentials are missing from the file'
    if ing['trust'] == 'trusted':
        return ', signer trusted'
    if ing['trust'] == 'untrusted':
        return ', signer not on a trust list'
    return ''


def to_text(report):
    """The report as plain text (for Copy, and for the tests)."""
    lines = [report['headline']]
    ai = ai_summary(report)
    if ai:
        lines.append(ai)
    lines.append('')
    lines.append('File: %s' % (report['file'] or '(none)'))
    if report.get('container'):
        lines.append('Manifest store: %s' % report['container']['container'])
    for e in report['explanation']:
        lines.append(e)
    if report['problems']:
        lines.append('')
        lines.append('Problems:')
        for p in report['problems']:
            lines.append('  %s (%s)' % (p['plain'], p['code']))

    def manifest(m, indent):
        pad = '  ' * indent
        s = m['signer']
        lines.append('%sClaim generator: %s' % (pad, m['claim_generator'] or '(not given)'))
        lines.append('%sSigned by: %s%s' % (pad, s['common_name'] or '(no name)',
                                           ', issued by ' + s['issuer'] if s['issuer'] else ''))
        lines.append('%sSigned at: %s' % (pad, s['time'] or '(no trusted time stamp)'))
        for a in m['authors']:
            lines.append('%sAuthor (as stated): %s' % (pad, a))
        for act in m['actions']:
            text = '%sAction: %s (%s)' % (pad, act['label'], act['action'])
            if act['software_agent']:
                text += ', software: %s' % act['software_agent']
            if act['digital_source_type']:
                d = act['digital_source_type']
                text += ', source type: %s (%s)' % (d['label'], d['term'])
            lines.append(text)
        for ing in m['ingredients']:
            lines.append('%sIngredient: %s [%s, %s]%s' % (
                pad, ing['title'] or '(no title)', ing['format'], ing['relationship_label'],
                ingredient_note(ing)))
            for p in ing['problems']:
                lines.append('%s  Problem: %s (%s)' % (pad, p['plain'], p['code']))
            if ing['manifest']:
                manifest(ing['manifest'], indent + 2)

    if report['active']:
        lines.append('')
        lines.append('Active manifest (%d in the file):' % report['manifest_count'])
        manifest(report['active'], 1)
    if report['ai']:
        lines.append('')
        lines.append('Generative AI:')
        for a in report['ai']:
            lines.append('  %s: %s, from %s' % (a['type']['term'], a['type']['definition'],
                                                a['source']))
    if report['xmp']:
        lines.append('')
        lines.append('XMP:')
        for x in report['xmp']:
            lines.append('  %s: %s (from %s)' % (x['kind'], x['value'], x['source']))
    lines.append('')
    if report.get('library') and report['library'].get('available'):
        lines.append('Checked with %s, offline, against: %s' % (
            report['library']['version'],
            ', '.join('%s (%d certificates)' % (t['name'], t['certs'])
                      for t in report['trust_lists']) or 'no trust list'))
    lines.extend(report['honest'])
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    # python3 c2pa_report.py FILE... [--json] [--trust PEM]
    args = sys.argv[1:]
    as_json = '--json' in args
    trust = None
    if '--trust' in args:
        i = args.index('--trust')
        trust = args[i + 1]
        del args[i:i + 2]
    for f in [a for a in args if a != '--json']:
        r = analyze(f, user_trust_file=trust)
        print(json.dumps(r, indent=1) if as_json else to_text(r))
