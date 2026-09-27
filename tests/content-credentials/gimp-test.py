# Runs inside GIMP without a window (tests/content-credentials/run.sh, which
# installs the plug-in into a throwaway profile): the procedure and its
# menu, the report on every test file GIMP can open (the same as
# expected.py wants), the XMP GIMP reads, an image changed in GIMP, a new
# image, an XCF, and what GIMP's export does to Content Credentials.
# Prints "CC PASS <check>" or "CC FAIL <check>: <why>" (or "CC SKIP"), and
# "CC GIMP failures: <n>" at the end.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sys
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio  # noqa: E402

SRC = os.environ['CC_SRC']
OUT = os.environ['CC_OUT']
HERE = os.path.join(SRC, 'tests', 'content-credentials')
IMAGES = os.path.join(HERE, 'images')
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(SRC, 'plug-ins', 'content-credentials'))
from expected import EXPECTED, check_report  # noqa: E402
import c2pa_report  # noqa: E402

PROC = 'plug-in-content-credentials'
pdb = Gimp.get_pdb()
Gegl.init(None)
failures = 0
passes = 0


def say(line):
    print(line, flush=True)


def result(name, wrong):
    global failures, passes
    if wrong:
        failures += 1
        say('CC FAIL %s: %s' % (name, '; '.join(wrong)))
    else:
        passes += 1
        say('CC PASS %s' % name)


def check(name, cond, why=''):
    result(name, [] if cond else [why or 'not so'])


def load(path):
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(path))


def report_of(image):
    proc = pdb.lookup_procedure(PROC)
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    values = proc.run(config)
    status = values.index(0)
    if status != Gimp.PDBStatusType.SUCCESS:
        raise RuntimeError('status %s: %s' % (status.value_nick, values.index(1)
                                              if values.length() > 1 else ''))
    return json.loads(values.index(1))


def export(image, path, xmp=False):
    """Exports as GIMP's export procedures do by default, or with XMP on."""
    ext = os.path.splitext(path)[1][1:]
    name = {'jpg': 'file-jpeg-export', 'png': 'file-png-export', 'webp': 'file-webp-export',
            'tif': 'file-tiff-export'}[ext]
    proc = pdb.lookup_procedure(name)
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_property('file', Gio.File.new_for_path(path))
    for prop in ('include-exif', 'include-xmp', 'include-iptc'):
        if xmp and config.find_property(prop) is not None:
            config.set_property(prop, True)
    status = proc.run(config).index(0)
    if status != Gimp.PDBStatusType.SUCCESS:
        raise RuntimeError('%s: %s' % (name, status.value_nick))


def guarded(name, func):
    try:
        func()
    except Exception as e:
        result(name, ['%s: %s' % (type(e).__name__, e)])
        traceback.print_exc()


# ------------------------------------------------------------ registration

def registration():
    proc = pdb.lookup_procedure(PROC)
    check('the procedure is registered', proc is not None)
    paths = proc.get_menu_paths()
    check('menu Image > Forensics', paths == ['<Image>/Image/Forensics'], str(paths))
    check('menu label', proc.get_menu_label() == '_Content Credentials...',
          proc.get_menu_label())
    names = [a.get_name() for a in proc.get_return_values()]
    check('returns the report', names == ['report'], str(names))


guarded('registration', registration)

# ------------------------------------------------------------ every test file

opened = {}
for name, exp in EXPECTED.items():
    path = os.path.join(IMAGES, name)
    try:
        image = load(path)
        if image is None:
            raise RuntimeError('no image')
    except Exception as e:
        say('CC SKIP GIMP cannot open %s: %s' % (name, e))
        continue
    try:
        r = report_of(image)
    except Exception as e:
        result('GIMP ' + name, ['%s: %s' % (type(e).__name__, e)])
        image.delete()
        continue
    wrong = check_report(name, r, exp)
    if r['file'] != path:
        wrong.append('checked %s, not the file the image came from' % r['file'])
    if r['image_dirty']:
        wrong.append('a freshly opened image is dirty')
    result('GIMP ' + name, wrong)
    opened[name] = r
    image.delete()

# (GIMP 3.2.6's file-heif crashes on the .heic sample of c2pa-rs, signed
# or not: "XMP Toolkit error 201", then a segmentation fault; the .heif
# sample opens)
FORMATS = ('png', 'webp', 'avif', 'heif', 'jxl', 'tif', 'gif', 'jpg')
check('GIMP opened the files of all formats (%s)' % ' '.join(FORMATS), all(
    ('generated/ai-generated.' + ext) in opened for ext in FORMATS),
    'not opened: %s' % [e for e in FORMATS if 'generated/ai-generated.' + e not in opened])
check('GIMP 3.2.6 still crashes on the .heic sample (remove this check when it opens)',
      'generated/ai-generated.heic' not in opened)

# XMP as GIMP read it
r = opened.get('generated/xmp-ai.jpg')
if r:
    check('xmp-ai.jpg: the digital source type is from GIMP\'s metadata', [
        x['source'] for x in r['xmp'] if x['kind'] == 'DigitalSourceType'] ==
        ['XMP metadata GIMP read from the file'], str(r['xmp']))
r = opened.get('generated/xmp-ai.png')
if r:
    check('xmp-ai.png: the digital source type is found (GIMP or the file)', [
        x['type']['term'] for x in r['xmp'] if x['kind'] == 'DigitalSourceType'] ==
        ['trainedAlgorithmicMedia'], str(r['xmp']))


# ------------------------------------------------------------ changed in GIMP

def changed():
    image = load(os.path.join(IMAGES, 'generated', 'ai-generated.png'))
    layer = image.get_layers()[0]
    Gimp.context_set_foreground(Gegl.Color.new('red'))
    layer.edit_fill(Gimp.FillType.FOREGROUND)
    r = report_of(image)
    check('changed in GIMP: the file on disk is checked, still valid',
          r['status'] == 'untrusted' and r['image_dirty'], '%s dirty=%s' % (
              r['status'], r['image_dirty']))
    check('changed in GIMP: explained', any('changed in GIMP' in e for e in r['explanation']),
          str(r['explanation']))
    image.delete()


guarded('changed in GIMP', changed)


def new_image():
    image = Gimp.Image.new(64, 64, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(image, 'bg', 64, 64, Gimp.ImageType.RGB_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    r = report_of(image)
    check('a new image: no file to check', r['status'] == 'nofile' and not r['file'],
          r['status'])
    image.delete()


guarded('new image', new_image)


def xcf():
    image = load(os.path.join(IMAGES, 'generated', 'ai-generated.png'))
    path = os.path.join(OUT, 'ai-generated.xcf')
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(path), None)
    image.delete()
    image = load(path)
    r = report_of(image)
    check('an XCF: not a C2PA format, and says why', r['status'] == 'unsupported' and
          r['file'] == path and any('XCF' in e for e in r['explanation']),
          '%s %s' % (r['status'], r['explanation']))
    image.delete()


guarded('XCF', xcf)


# ------------------------------------------------------------ export

def exports():
    """What GIMP's export does to Content Credentials (the finding for
    docs/content-credentials-plan.md): printed as CC EXPORT lines."""
    kept = []
    for name in ('adobe-20220124-C.jpg', 'c2pa-rs-ocsp.jpg', 'generated/ai-generated.png',
                 'generated/xmp-ai.jpg'):
        image = load(os.path.join(IMAGES, name))
        base = os.path.splitext(os.path.basename(name))[0]
        for ext in ('jpg', 'png', 'webp', 'tif'):
            for xmp in (False, True):
                path = os.path.join(OUT, 'export-%s%s.%s' % (base, '-xmp' if xmp else '', ext))
                export(image, path, xmp)
                r = c2pa_report.analyze(path)
                dst = [x['type']['term'] for x in r['xmp'] if x['kind'] == 'DigitalSourceType']
                prov = [x['value'] for x in r['xmp'] if x['kind'] == 'provenance']
                say('CC EXPORT %s -> %s (%s): %s, XMP DigitalSourceType %s, dcterms:provenance '
                    '%s' % (name, ext, 'metadata on' if xmp else 'defaults', r['status'],
                            dst or 'none', 'kept' if prov else 'none'))
                if r['status'] != 'none':
                    kept.append('%s %s' % (name, ext))
                if xmp and prov and not any('removed when the file was saved' in e
                                            for e in r['explanation']):
                    kept.append('%s %s: dangling provenance not explained' % (name, ext))
        image.delete()
    check('GIMP 3.2 export drops the C2PA manifest store (jpg png webp tif, with and without '
          'metadata)', not kept, str(kept))


guarded('export', exports)


def copied_store():
    """Keeping the credentials by copying the manifest store into the
    exported file (the naive way) makes them tampered: the hash is of the
    original file's bytes."""
    original = os.path.join(IMAGES, 'adobe-20220124-C.jpg')
    exported = os.path.join(OUT, 'export-adobe-20220124-C.jpg')
    data = open(original, 'rb').read()
    app11 = b''.join(data[pos - 4:pos + length]
                     for marker, pos, length in c2pa_report._jpeg_segments(data)
                     if marker == 0xeb)
    out = open(exported, 'rb').read()
    path = os.path.join(OUT, 'export-with-copied-store.jpg')
    open(path, 'wb').write(out[:2] + app11 + out[2:])
    r = c2pa_report.analyze(path)
    say('CC EXPORT the manifest store of adobe-20220124-C.jpg copied into GIMP\'s export: %s, '
        '%s' % (r['status'], [p['code'] for p in r['problems']]))
    check('a manifest store copied into an export is tampered (data hash)',
          r['status'] == 'tampered' and 'assertion.dataHash.mismatch' in
          [p['code'] for p in r['problems']], r['status'])


guarded('copied store', copied_store)

say('CC GIMP passed: %d' % passes)
say('CC GIMP failures: %d' % failures)
