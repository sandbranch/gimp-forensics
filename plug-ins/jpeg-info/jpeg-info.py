#!/usr/bin/env python3
# JPEG Info: Image > Forensics > JPEG Info... shows what the JPEG file an
# image was opened from says about how it was saved: the quantisation
# tables and the quality of the last save, whether the tables are those of
# a known camera or program (JPEGsnoop's signatures), hints of an earlier
# save (double compression), the metadata that tells of software, and the
# Exif thumbnail against the image. "Add Thumbnail Layers" puts the
# thumbnail, scaled to the image, over a copy of the image in Difference
# mode.
#
# Read only: the file on disk is read, never written. The work is done by
# jpeg_report.py next to this file (pure Python). Non-interactively the
# procedure returns the report as JSON (for the Forensics Workbench, which
# takes the quality from it, and for scripts); with add-thumbnail it adds
# the thumbnail layers too.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sys

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import Gimp, GLib, GObject, GdkPixbuf, Gio  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jpeg_report  # noqa: E402

PROC = 'plug-in-forensics-jpeg-info'
MENU = '<Image>/Image/Forensics'
BINARY = 'jpeg-info'
GROUP = 'EXIF Thumbnail'


def source_file(image):
    """the file on disk the image came from: the imported file if there is
    one, else the image's file"""
    for getter in ('get_imported_file', 'get_file', 'get_xcf_file'):
        f = getattr(image, getter, None)
        f = f() if f else None
        if f is not None and f.get_path():
            return f.get_path()
    return None


def pixbuf_from_bytes(data):
    loader = GdkPixbuf.PixbufLoader()
    loader.write(data)
    loader.close()
    return loader.get_pixbuf()


def rgb_bytes(pb):
    """the pixels of a pixbuf as RGB bytes, 3 per pixel, no row padding"""
    if pb.get_has_alpha():
        pb = pb.composite_color_simple(pb.get_width(), pb.get_height(),
                                       GdkPixbuf.InterpType.NEAREST, 255, 1, 0, 0)
    w, h, stride, n = pb.get_width(), pb.get_height(), pb.get_rowstride(), pb.get_n_channels()
    px = pb.get_pixels()
    out = bytearray()
    for y in range(h):
        row = px[y * stride:y * stride + w * n]
        if n == 3:
            out += row
        else:
            for x in range(w):
                out += row[x * n:x * n + 3]
    return bytes(out)


def compare_thumbnail(path, thumb_bytes, report):
    """decodes the thumbnail and the file (scaled down), finds black bars,
    and compares: the numbers go into report['thumbnail']['compare'];
    returns (thumbnail pixbuf, image pixbuf at its size, box) or None"""
    t = report.get('thumbnail')
    try:
        thumb = pixbuf_from_bytes(thumb_bytes)
    except GLib.Error as e:
        if t is not None:
            t['compare_error'] = 'the thumbnail cannot be decoded: %s' % e.message
        return None
    tw, th = thumb.get_width(), thumb.get_height()
    x0, y0, x1, y1 = jpeg_report.content_box(rgb_bytes(thumb), tw, th)
    bw, bh = x1 - x0, y1 - y0
    try:
        img = GdkPixbuf.Pixbuf.new_from_file_at_scale(path, bw, bh, False)
    except GLib.Error as e:
        t['compare_error'] = 'the image cannot be decoded: %s' % e.message
        return None
    crop = thumb.new_subpixbuf(x0, y0, bw, bh).copy()
    c = jpeg_report.compare_pixels(rgb_bytes(crop), rgb_bytes(img), bw, bh)
    fr = report['frame']
    t['box'] = [x0, y0, x1, y1]
    t['box_aspect'] = bw / bh if bh else None
    t['compare'] = c
    ia = fr['width'] / fr['height']
    if t['box_aspect'] and abs(t['box_aspect'] / ia - 1) > 0.02:
        report['hints'].append(('edit', 'Inside its black bars the thumbnail has another shape '
                                        '(%.3f) than the image (%.3f): the image was cropped or '
                                        'resized after the thumbnail was made.'
                                % (t['box_aspect'], ia)))
    elif c and (c['mean'] > 12 or c['share_over_32'] > 0.05):
        report['hints'].append(('edit', 'The thumbnail differs from the image (%.1f levels on '
                                        'average, %.1f %% of the pixels by more than 32): the '
                                        'image changed after the thumbnail was made, or the '
                                        'thumbnail was made differently. Look at the difference.'
                                % (c['mean'], 100 * c['share_over_32'])))
    elif c:
        report['hints'].append(('ok', 'The thumbnail looks like the image (%.1f levels apart on '
                                      'average, at the thumbnail\'s size).' % c['mean']))
    return thumb, img, (x0, y0, x1, y1)


def exif_thumbnail(path):
    with open(path, 'rb') as f:
        data = f.read()
    info = jpeg_report.parse(data)
    return (info.get('exif') or {}).get('thumbnail')


def make_report(image):
    path = source_file(image)
    report = jpeg_report.analyze(path)
    report['image_dirty'] = bool(image.is_dirty())
    thumb = None
    if report.get('is_jpeg') and report.get('thumbnail'):
        tb = exif_thumbnail(path)
        if tb:
            thumb = compare_thumbnail(path, tb, report)
    if path and report.get('is_jpeg') and report['image_dirty']:
        report['hints'].append(('note', 'The image was changed in GIMP since it was opened: '
                                        'this report is on the file on disk.'))
    return report, thumb


def add_thumbnail_layers(image, path, report):
    """a group at the top of the image: a copy of the image as it looks and
    the Exif thumbnail over it, stretched to the image (inside its black
    bars), in Difference mode. Returns the group, or None without a
    thumbnail."""
    tb = exif_thumbnail(path) if path else None
    if not tb:
        return None
    t = report.get('thumbnail') or {}
    tmp = Gimp.temp_file('jpg').get_path()
    try:
        with open(tmp, 'wb') as f:
            f.write(tb)
        layer = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image,
                                     Gio.File.new_for_path(tmp))
    finally:
        os.unlink(tmp)
    if layer is None:
        return None
    image.undo_group_start()
    try:
        base = Gimp.Layer.new_from_visible(image, image, 'Image')
        group = Gimp.GroupLayer.new(image, GROUP)
        image.insert_layer(group, None, 0)
        image.insert_layer(base, group, 0)
        image.insert_layer(layer, group, 0)
        layer.set_name('Thumbnail (Difference)')
        box = t.get('box')
        if box:
            x0, y0, x1, y1 = box
            if (x1 - x0, y1 - y0) != (layer.get_width(), layer.get_height()):
                layer.resize(x1 - x0, y1 - y0, -x0, -y0)
        layer.set_offsets(0, 0)
        layer.scale(image.get_width(), image.get_height(), False)
        layer.set_offsets(0, 0)
        layer.set_mode(Gimp.LayerMode.DIFFERENCE)
        image.set_selected_layers([layer])
    finally:
        image.undo_group_end()
    return group


# ------------------------------------------------------------ the dialog

LEVEL_BADGE = {'edit': ('#b3261e', 'SIGN'), 'note': ('#5f6368', 'NOTE'), 'ok': ('#1b7a34', 'OK')}


def tree_rows(rep):
    rows = []
    q = rep['quality']
    rows.append((0, 'Last saved quality', '%s' % q.get('estimate')))
    rows.append((1, 'How', q.get('method')))
    for t in q.get('per_table', []):
        rows.append((1, 'Table %d' % t['id'], 'nearest IJG quality %d%s, level %.1f %%' % (
            t['closest_ijg'], ' (exact)' if t['exact'] else ', mean deviation %.2f' % t['deviation'],
            t['level'])))
    for t in rep['tables']:
        rows.append((0, 'Quantisation table %d' % t['id'], '%d bit' % t['precision']))
        for r in range(8):
            rows.append((1, 'row %d' % (r + 1), ' '.join('%3d' % v for v in t['values'][8 * r:8 * r + 8])))
    fr = rep['frame']
    rows.append((0, 'Frame', '%d x %d, %s, %s, %d bit' % (fr['width'], fr['height'], fr['kind'],
                                                        fr['subsampling'], fr['precision'])))
    rows.append((1, 'Huffman tables', str(rep['huffman'])))
    rows.append((1, 'Scans', str(rep['scans'])))
    rows.append((1, 'Restart interval', str(rep['restart_interval'])))
    if rep.get('trailer'):
        rows.append((1, 'After the end of the image', '%d bytes' % rep['trailer']))
    a = rep['assessment']
    rows.append((0, 'JPEGsnoop assessment', 'class %d: %s' % (a['class'], a['text'])))
    for r in a['reasons']:
        rows.append((1, 'because', r))
    s = rep['signatures']
    rows.append((0, 'Compression signature', '%s, %d match%s in the database' % (
        s['signature'], len(s['matches']), '' if len(s['matches']) == 1 else 'es')))
    for m in s['matches'][:60]:
        if m['kind'] == 'cam':
            rows.append((1, 'camera', '%s %s %s%s%s' % (
                m['make'], m['model'], ('"%s"' % m['quality']) if m['quality'] else '',
                ' (the Exif camera)' if m['make_model_matches'] else '',
                '' if m['subsampling_matches'] else ' (other subsampling)')))
        else:
            rows.append((1, 'software', '%s %s' % (m['name'], m['quality'])))
    d = rep.get('double')
    if d:
        verdict = {'likely': 'likely: an earlier save with larger steps', 'weak': 'a weak hint',
                   'none': 'no sign', 'unknown': 'cannot tell'}.get(d['verdict'], d['verdict'])
        rows.append((0, 'Double compression', verdict + (' (%s)' % d['reason']
                                                         if d.get('reason') else '')))
        if d.get('primary'):
            p = d['primary']
            rows.append((1, 'First save', 'about quality %d (%s), %d of %d steps exact' % (
                p['quality'], p['family'], p['matches'], p['of'])))
        if 'blocks' in d:
            rows.append((1, 'Blocks read', '%d (rows %d of %d)' % (
                d['blocks'], d.get('decoded_rows', 0), d.get('total_rows', 0))))
        for f in d.get('frequencies', []):
            if 'score' in f:
                rows.append((1, 'zigzag %d' % f['zigzag'], 'last step %d, best first step %s, '
                             '%.3f nats per block' % (f['q2'], f['q1'] if f['q1'] > 1 else '(none)',
                                                      f['score'])))
    t = rep.get('thumbnail')
    if t:
        rows.append((0, 'Exif thumbnail', '%s x %s, %d bytes' % (t.get('width'), t.get('height'),
                                                                 t['size'])))
        if t.get('quality'):
            rows.append((1, 'Its quality', '%s (%s)' % (t['quality'], t.get('quality_method'))))
        if t.get('box'):
            rows.append((1, 'Inside black bars', '%d, %d to %d, %d' % tuple(t['box'])))
        c = t.get('compare')
        if c:
            rows.append((1, 'Against the image', 'mean difference %.1f levels, 99th percentile '
                         '%d, %.1f %% over 32' % (c['mean'], c['p99'], 100 * c['share_over_32'])))
        for m in t.get('matches', [])[:8]:
            rows.append((1, 'Its tables match', '%s %s %s' % (m['make'] or m['name'], m['model'],
                                                              m['quality'])))
    meta = rep['metadata']
    ex = meta.get('exif')
    if ex:
        rows.append((0, 'Exif', ''))
        for k, v in list(ex['camera'].items()) + list(ex['exif'].items()):
            rows.append((1, k, str(v)))
        rows.append((1, 'Maker notes', '%d bytes' % ex['makernote'] if ex['makernote'] else 'none'))
        rows.append((1, 'GPS', 'yes' if ex['gps'] else 'no'))
    for key, label in (('xmp', 'XMP'), ('icc', 'ICC profile'), ('photoshop', 'Photoshop'),
                       ('adobe', 'Adobe (APP14)'), ('jfif', 'JFIF')):
        v = meta.get(key)
        if v:
            rows.append((0, label, ''))
            for k2, v2 in v.items():
                rows.append((1, k2, ', '.join(map(str, v2)) if isinstance(v2, list) else str(v2)))
    for c in meta.get('comments') or []:
        rows.append((0, 'Comment', c))
    rows.append((0, 'Segments', '%d' % len(rep['segments'])))
    for sgm in rep['segments']:
        rows.append((1, sgm['marker'], 'at %d, %d bytes' % (sgm['offset'], sgm['length'])))
    return rows


def show_dialog(rep, thumb, image, path):
    gi.require_version('GimpUi', '3.0')
    gi.require_version('Gtk', '3.0')
    from gi.repository import GimpUi, Gtk, Gdk, Pango
    GimpUi.init(BINARY)

    def label(markup, wrap=True, selectable=False):
        w = Gtk.Label()
        w.set_markup(markup)
        w.set_xalign(0.0)
        w.set_line_wrap(wrap)
        if wrap:
            w.set_line_wrap_mode(Pango.WrapMode.WORD_CHAR)
            w.set_width_chars(90)
            w.set_max_width_chars(90)
        w.set_selectable(selectable)
        return w

    esc = GLib.markup_escape_text
    dialog = GimpUi.Dialog(title='JPEG Info', role=BINARY, modal=True)
    if thumb:
        dialog.add_button('_Add Thumbnail Layers', 2)
    dialog.add_button('_Copy Report', 1)
    dialog.add_button('_Close', Gtk.ResponseType.CLOSE)
    dialog.set_default_size(820, 720)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
    box.set_border_width(12)
    dialog.get_content_area().pack_start(box, True, True, 0)
    if rep.get('error'):
        box.pack_start(label('<span size="large" weight="bold">%s</span>' % esc(rep['error'])),
                       False, False, 0)
    else:
        q = rep['quality']
        box.pack_start(label('<span size="large" weight="bold">Last saved at quality %s%s</span>'
                             % (q.get('estimate'), ' (standard tables)' if q.get('exact')
                                else ' (estimated)')), False, False, 0)
        for level, text in rep['hints']:
            colour, word = LEVEL_BADGE.get(level, ('#5f6368', ''))
            row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            row.pack_start(label('<span background="%s" foreground="#ffffff" weight="bold"> %s '
                                 '</span>' % (colour, word), wrap=False), False, False, 0)
            row.pack_start(label(esc(text)), True, True, 0)
            box.pack_start(row, False, False, 0)
        if thumb:
            tpb, ipb, (x0, y0, x1, y1) = thumb
            size = 200
            w, h = x1 - x0, y1 - y0
            sw, sh = (size, max(1, round(size * h / w))) if w >= h else (max(1, round(size * w / h)), size)
            crop = tpb.new_subpixbuf(x0, y0, w, h)
            a = crop.scale_simple(sw, sh, GdkPixbuf.InterpType.NEAREST)
            b = ipb.scale_simple(sw, sh, GdkPixbuf.InterpType.NEAREST)
            diff = diff_pixbuf(a, b)
            strip = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
            for pb, cap in ((a, 'Exif thumbnail'), (b, 'The image, scaled'),
                            (diff, 'Difference x 4')):
                col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
                col.pack_start(Gtk.Image.new_from_pixbuf(pb), False, False, 0)
                col.pack_start(label('<small>%s</small>' % cap, wrap=False), False, False, 0)
                strip.pack_start(col, False, False, 0)
            box.pack_start(strip, False, False, 0)
        store = Gtk.TreeStore(str, str)
        parents = {}
        for depth, item, value in tree_rows(rep):
            parent = parents.get(depth - 1) if depth else None
            parents[depth] = store.append(parent, [item, str(value)])
        view = Gtk.TreeView(model=store)
        view.set_headers_visible(False)
        for i, width in enumerate((230, 440)):
            cell = Gtk.CellRendererText()
            cell.set_property('wrap-mode', Pango.WrapMode.WORD_CHAR)
            cell.set_property('wrap-width', width)
            if i:
                cell.set_property('family', 'monospace')
            view.append_column(Gtk.TreeViewColumn('', cell, text=i))
        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scroll.set_shadow_type(Gtk.ShadowType.IN)
        scroll.set_min_content_height(260)
        scroll.add(view)
        box.pack_start(scroll, True, True, 0)
    box.pack_start(label('<small>File: %s</small>' % esc(str(rep.get('file'))), selectable=True),
                   False, False, 0)
    box.pack_start(label('<small>%s</small>' % esc(' '.join(rep['honest']))), False, False, 0)
    dialog.show_all()
    while True:
        response = dialog.run()
        if response == 1:
            clip = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
            clip.set_text(jpeg_report.to_text(rep), -1)
            clip.store()
            continue
        if response == 2:
            add_thumbnail_layers(image, path, rep)
            Gimp.displays_flush()
        break
    dialog.destroy()


def diff_pixbuf(a, b, gain=4):
    w, h = a.get_width(), a.get_height()
    pa, pb = rgb_bytes(a), rgb_bytes(b)
    out = bytes(min(255, abs(x - y) * gain) for x, y in zip(pa, pb))
    return GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(out), GdkPixbuf.Colorspace.RGB,
                                           False, 8, w, h, 3 * w)


# ------------------------------------------------------------ the procedure

def run(procedure, run_mode, image, drawables, config, data):
    try:
        rep, thumb = make_report(image)
        path = source_file(image)
        group = None
        if run_mode == Gimp.RunMode.INTERACTIVE:
            show_dialog(rep, thumb, image, path)
        elif config.get_property('add-thumbnail') and rep.get('is_jpeg'):
            group = add_thumbnail_layers(image, path, rep)
        rep['thumbnail_group'] = group.get_id() if group else None
    except Exception as e:  # never fail silently: say what went wrong
        return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                           GLib.Error('JPEG Info: %s: %s' % (type(e).__name__, e)))
    result = procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, None)
    if result.length() > 1:
        result.remove(1)
    result.insert(1, GObject.Value(GObject.TYPE_STRING, json.dumps(rep)))
    return result


class JpegInfo(Gimp.PlugIn):

    def do_set_i18n(self, name):
        return False

    def do_query_procedures(self):
        return [PROC]

    def do_create_procedure(self, name):
        p = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, run, None)
        p.set_image_types('*')
        p.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE |
                               Gimp.ProcedureSensitivityMask.DRAWABLES |
                               Gimp.ProcedureSensitivityMask.NO_DRAWABLES)
        p.set_menu_label('_JPEG Info...')
        p.add_menu_path(MENU)
        p.set_documentation(
            'Show what the JPEG file of the image says about how it was saved',
            'Reads the JPEG file the image was opened from: the quantisation tables and the '
            'quality of the last save, whether they are those of a known camera or program '
            '(JPEGsnoop\'s signatures and assessment), hints of double compression from the '
            'DCT coefficients, the metadata of software, and the Exif thumbnail against the '
            'image. Non-interactively returns the report as JSON; with add-thumbnail also adds '
            'the thumbnail over a copy of the image in Difference mode. Indicators, not proof.',
            name)
        p.set_attribution('David', 'David, GPL-3.0-or-later', '2026')
        p.add_boolean_argument('add-thumbnail', 'Add _thumbnail layers',
                               'Add the Exif thumbnail over a copy of the image, in Difference '
                               'mode (non-interactive runs)', False, GObject.ParamFlags.READWRITE)
        p.add_string_return_value('report', 'Report', 'The report, as JSON', '',
                                  GObject.ParamFlags.READWRITE)
        return p


if __name__ == '__main__':
    Gimp.main(JpegInfo.__gtype__, sys.argv)
