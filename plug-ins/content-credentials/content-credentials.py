#!/usr/bin/env python3
# Content Credentials: shows the C2PA provenance of the file an image was
# opened from (Image > Forensics > Content Credentials...): who signed it
# and when, with what software, the actions and ingredients the
# credentials declare, whether they say generative AI was used (the IPTC
# digital source type), and whether they check out: valid, valid but from a
# signer on no trust list, tampered, or none at all. Also the digital
# source type in the image's XMP metadata, which anyone can write.
#
# Read only. The check is on the file on disk, offline; the work is done by
# c2pa_report.py next to this file, with the C2PA reference implementation
# (c2pa-rs, through c2pa-python) that fetch-deps.py puts into vendor/.
#
# Non-interactively the procedure returns the report as JSON, for scripts
# and for the tests.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sys

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('GimpUi', '3.0')
gi.require_version('GExiv2', '0.10')
gi.require_version('Gtk', '3.0')
from gi.repository import Gimp, GimpUi, GLib, GObject, Gtk, Gdk, Pango  # noqa: E402
from gi.repository import GExiv2  # noqa: E402,F401 (the tag methods of Gimp.Metadata)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c2pa_report  # noqa: E402

PROC = 'plug-in-content-credentials'
MENU = '<Image>/Image/Forensics'
BINARY = 'content-credentials'
AUTHOR = 'David'
COPYRIGHT = 'David, GPL-3.0-or-later'
DATE = '2026'

XMP_TAGS = {
    'DigitalSourceType': 'Xmp.iptcExt.DigitalSourceType',
    'provenance': 'Xmp.dcterms.provenance',
    'CreatorTool': 'Xmp.xmp.CreatorTool',
}


def user_trust_file():
    """Your own trust anchors (PEM), if you put some there: in the GIMP
    folder, content-credentials/trust-anchors.pem."""
    return os.path.join(Gimp.directory(), 'content-credentials', 'trust-anchors.pem')


def source_file(image):
    """The file on disk the image came from: the imported file (JPEG, PNG,
    ...) if there is one, else the XCF, else None."""
    for getter in ('get_imported_file', 'get_file', 'get_xcf_file'):
        f = getattr(image, getter, None)
        f = f() if f else None
        if f is not None and f.get_path():
            return f.get_path()
    return None


def gimp_xmp(image):
    """The XMP tags GIMP read with the image: {name: [values]}, or None."""
    md = image.get_metadata()
    if md is None:
        return None
    found = {}
    for key, tag in XMP_TAGS.items():
        value = None
        try:
            value = md.try_get_tag_string(tag)
        except GLib.Error:
            value = None
        except AttributeError:
            try:
                value = md.get_tag_string(tag)
            except Exception:
                value = None
        found[key] = [value.strip()] if value and value.strip() else []
    return found


def make_report(image):
    path = source_file(image)
    report = c2pa_report.analyze(path, user_trust_file=user_trust_file(),
                                 gimp_xmp=gimp_xmp(image),
                                 gimp_xmp_source='XMP metadata GIMP read from the file')
    report['image_dirty'] = bool(image.is_dirty())
    if path and path.lower().endswith(('.xcf', '.xcf.gz', '.xcf.bz2', '.xcf.xz')):
        report['explanation'].append('This is an XCF file: GIMP\'s own format does not hold '
                                     'Content Credentials. Open the file it was made from to '
                                     'check that one.')
    if path and report['image_dirty']:
        report['explanation'].append('The image was changed in GIMP since it was opened. '
                                     'This check is on the file on disk, not on what you see '
                                     'now.')
    return report


# ------------------------------------------------------------ the dialog

# a coloured badge for each status (the icon themes of GIMP differ)
STATUS_BADGE = {
    'trusted': ('#1b7a34', 'VALID'),
    'untrusted': ('#9a6700', 'UNKNOWN SIGNER'),
    'tampered': ('#b3261e', 'TAMPERED'),
    'invalid': ('#b3261e', 'INVALID'),
    'none': ('#5f6368', 'NONE'),
    'remote': ('#5f6368', 'ONLINE ONLY'),
    'unreadable': ('#b3261e', 'UNREADABLE'),
    'unavailable': ('#9a6700', 'NOT CHECKED'),
    'unsupported': ('#5f6368', 'NOT CHECKED'),
    'nofile': ('#5f6368', 'NO FILE'),
}


def esc(text):
    return GLib.markup_escape_text(str(text))


def label(markup, wrap=True, selectable=False, xalign=0.0):
    w = Gtk.Label()
    w.set_markup(markup)
    w.set_xalign(xalign)
    w.set_line_wrap(wrap)
    if wrap:
        w.set_line_wrap_mode(Pango.WrapMode.WORD_CHAR)
        # (as wide as the dialog: a wrapped label's minimum width is a few
        # characters, and GTK sizes the window for that, very tall)
        w.set_width_chars(90)
        w.set_max_width_chars(90)
    w.set_selectable(selectable)
    return w


def tree_rows(report):
    """(depth, item, value) rows for the details tree."""
    rows = []

    def manifest(m, depth):
        s = m['signer']
        rows.append((depth, 'Claim generator', m['claim_generator'] or '(not given)'))
        rows.append((depth, 'Signed by', (s['common_name'] or '(no name)') +
                     (', issued by ' + s['issuer'] if s['issuer'] else '')))
        rows.append((depth, 'Signed at', s['time'] or '(no trusted time stamp)'))
        for a in m['authors']:
            rows.append((depth, 'Author (as stated)', a))
        if m['actions']:
            rows.append((depth, 'Actions', str(len(m['actions']))))
            for act in m['actions']:
                rows.append((depth + 1, act['label'], act['action']))
                if act['software_agent']:
                    rows.append((depth + 2, 'Software', act['software_agent']))
                if act['when']:
                    rows.append((depth + 2, 'When', act['when']))
                if act['description']:
                    rows.append((depth + 2, 'Description', act['description']))
                d = act['digital_source_type']
                if d:
                    rows.append((depth + 2, 'Source type', '%s%s (IPTC %s)' % (
                        'Generative AI: ' if d['ai'] else '', d['label'], d['term'])))
        if m['ingredients']:
            rows.append((depth, 'Ingredients', str(len(m['ingredients']))))
            for ing in m['ingredients']:
                rows.append((depth + 1, ing['title'] or '(no title)', '%s, %s%s' % (
                    ing['format'], ing['relationship_label'],
                    c2pa_report.ingredient_note(ing))))
                for p in ing['problems']:
                    rows.append((depth + 2, 'Problem', '%s (%s)' % (p['plain'], p['code'])))
                if ing['manifest']:
                    manifest(ing['manifest'], depth + 2)

    if report['active']:
        rows.append((0, 'Active manifest', '%s (%d in the file)' % (
            report['active']['title'] or report['active']['label'],
            report['manifest_count'])))
        manifest(report['active'], 1)
    if report['problems']:
        rows.append((0, 'Validation', '%d finding%s' % (
            len(report['problems']), '' if len(report['problems']) == 1 else 's')))
        for p in report['problems']:
            rows.append((1, p['code'], p['plain']))
    if report['ai']:
        rows.append((0, 'Generative AI', c2pa_report.ai_summary(report) or ''))
        for a in report['ai']:
            rows.append((1, a['type']['term'], a['type']['definition']))
            rows.append((2, 'From', a['source']))
    if report['xmp']:
        rows.append((0, 'XMP metadata', 'not signed: anyone can write or remove it'))
        for x in report['xmp']:
            value = x['value']
            if x['type']:
                value = '%s (%s)' % (x['type']['label'], x['type']['term'])
            rows.append((1, x['kind'], value))
            rows.append((2, 'From', x['source']))
    if report.get('container'):
        rows.append((0, 'Stored in', report['container']['container']))
    lib = report.get('library') or {}
    if lib.get('available'):
        rows.append((0, 'Checked with', '%s, offline' % lib.get('version')))
        for t in report['trust_lists']:
            rows.append((1, t['name'], '%d certificates' % t['certs']))
    return rows


class Dialog:

    def __init__(self, report):
        self.report = report
        self.dialog = GimpUi.Dialog(title='Content Credentials', role=BINARY, modal=True)
        self.dialog.add_button('_Copy Report', 1)
        self.dialog.add_button('_Close', Gtk.ResponseType.CLOSE)
        self.dialog.set_default_size(780, 660)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.set_border_width(12)
        self.dialog.get_content_area().pack_start(box, True, True, 0)

        head = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        colour, word = STATUS_BADGE.get(report['status'], ('#5f6368', ''))
        head.pack_start(label('<span background="%s" foreground="#ffffff" weight="bold">'
                              '  %s  </span>' % (colour, esc(word)), wrap=False),
                        False, False, 0)
        head.pack_start(label('<span size="large" weight="bold">%s</span>'
                              % esc(report['headline'])), True, True, 0)
        box.pack_start(head, False, False, 0)

        ai = c2pa_report.ai_summary(report)
        if ai:
            frame = Gtk.Frame()
            inner = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            inner.set_border_width(8)
            inner.pack_start(label('<b>%s</b>' % esc(ai)), False, False, 0)
            first = report['ai'][0]
            inner.pack_start(label('IPTC: "%s" means: %s. Found in: %s.' % (
                esc(first['type']['label']), esc(first['type']['definition']),
                esc(first['source']))), False, False, 0)
            frame.add(inner)
            box.pack_start(frame, False, False, 0)

        for e in report['explanation']:
            box.pack_start(label(esc(e)), False, False, 0)
        box.pack_start(label('<small>File: %s</small>' % esc(report['file'] or '(none)'),
                             selectable=True), False, False, 0)

        rows = tree_rows(report)
        if rows:
            store = Gtk.TreeStore(str, str)
            parents = {}
            for depth, item, value in rows:
                parent = parents.get(depth - 1) if depth else None
                parents[depth] = store.append(parent, [item, value])
            view = Gtk.TreeView(model=store)
            view.set_headers_visible(False)
            for i, (title, width) in enumerate((('Item', 250), ('Value', 360))):
                cell = Gtk.CellRendererText()
                cell.set_property('wrap-mode', Pango.WrapMode.WORD_CHAR)
                cell.set_property('wrap-width', width)
                cell.set_property('yalign', 0.0)
                col = Gtk.TreeViewColumn(title, cell, text=i)
                col.set_resizable(True)
                view.append_column(col)
            view.expand_all()
            scroll = Gtk.ScrolledWindow()
            scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
            scroll.set_shadow_type(Gtk.ShadowType.IN)
            # the tree scrolls; the dialog keeps its size, whatever the
            # number of manifests
            scroll.set_min_content_height(240)
            scroll.set_max_content_height(360)
            scroll.set_propagate_natural_height(False)
            scroll.add(view)
            box.pack_start(scroll, True, True, 0)
            self.view = view

        box.pack_start(label('<small>%s</small>' % esc(' '.join(report['honest']))),
                       False, False, 0)
        self.dialog.show_all()

    def run(self):
        while True:
            response = self.dialog.run()
            if response == 1:
                clip = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
                clip.set_text(c2pa_report.to_text(self.report), -1)
                clip.store()
                continue
            break
        self.dialog.destroy()


# ------------------------------------------------------------ the procedure

def run(procedure, run_mode, image, drawables, config, data):
    try:
        report = make_report(image)
    except Exception as e:  # never fail silently: say what went wrong
        return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                           GLib.Error('Content Credentials: %s: %s'
                                                      % (type(e).__name__, e)))
    if run_mode == Gimp.RunMode.INTERACTIVE:
        GimpUi.init(BINARY)
        Dialog(report).run()
    result = procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, None)
    # (new_return_values fills in the default of each return value)
    if result.length() > 1:
        result.remove(1)
    result.insert(1, GObject.Value(GObject.TYPE_STRING, json.dumps(report)))
    return result


class ContentCredentials(Gimp.PlugIn):

    def do_set_i18n(self, name):
        return False

    def do_query_procedures(self):
        return [PROC]

    def do_create_procedure(self, name):
        procedure = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, run, None)
        procedure.set_image_types('*')
        procedure.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE |
                                       Gimp.ProcedureSensitivityMask.DRAWABLES |
                                       Gimp.ProcedureSensitivityMask.NO_DRAWABLES)
        procedure.set_menu_label('_Content Credentials...')
        procedure.add_menu_path(MENU)
        procedure.set_documentation(
            'Show the Content Credentials (C2PA) of the file the image came from',
            'Reads and validates the C2PA manifest store of the image\'s file on disk, '
            'offline: signer, time, claim generator, actions, digital source type '
            '(generative AI), ingredients, and whether the credentials are valid, from an '
            'untrusted signer, tampered or missing. Also the IPTC digital source type in the '
            'XMP metadata. Non-interactively, returns the report as JSON.',
            name)
        procedure.set_attribution(AUTHOR, COPYRIGHT, DATE)
        procedure.add_string_return_value('report', 'Report', 'The report, as JSON', '',
                                          GObject.ParamFlags.READWRITE)
        return procedure


if __name__ == '__main__':
    Gimp.main(ContentCredentials.__gtype__, sys.argv)
