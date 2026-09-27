#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Forensics Workbench for GIMP 3
# Copyright 2026 David
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Forensics Workbench: Image > Forensics > Analyze Image...

Adds a layer group "Forensics" at the top of the image with one copy of
the image as it looks (the visible layers, flattened) per analysis, each
carrying its analysis as a non-destructive filter: Error Level Analysis,
JPEG Ghost, Noise Analysis, Luminance Gradient, Clone Detection and
Principal Components (the forensics: operations of this repository), a
level sweep (gegl:levels on a narrow band of brightness), and the HSV and
LAB channels (gegl:component-extract). Only the top analysis is visible;
switch between them in the Layers dialog, and change an analysis's
settings by editing its filter (the fx icon of the layer). The image's
own layers are not touched.
"""

import math
import sys
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, GObject, GLib

PLUG_IN_BINARY = 'forensics-workbench'
PROC_NAME = 'python-fu-forensics-workbench'
GROUP_NAME = 'Forensics'
PARASITE = 'forensics-workbench'
MENU = '<Image>/Image/Forensics'


def srgb_to_linear(v):
    v = min(max(v, 0.0), 1.0)
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def level_sweep_props(config):
    """gegl:levels works in linear light: the band of encoded values
    position +- width / 2 in linear values"""
    p = config.get_property('sweep-position')
    w = config.get_property('sweep-width')
    lo = srgb_to_linear(p - w / 2)
    hi = srgb_to_linear(p + w / 2)
    if hi <= lo:
        hi = lo + 1e-4
    return {'in-low': lo, 'in-high': hi}


# The analyses, top of the group first: (switch, layer name, operation,
# settings from the config). Each switch turns on one or more layers.
def analyses(config):
    c = config.get_property
    return [
        ('ela', 'Error Level Analysis', 'forensics:error-level',
         {'quality': c('ela-quality'), 'scale': c('ela-scale')}),
        ('ghost', 'JPEG Ghost', 'forensics:jpeg-ghost',
         {'quality': c('ghost-quality')}),
        ('noise', 'Noise Analysis', 'forensics:noise',
         {'amplitude': c('noise-amplitude')}),
        ('gradient', 'Luminance Gradient', 'forensics:luminance-gradient', {}),
        ('clone', 'Clone Detection', 'forensics:clone-detect',
         {'tolerance': c('clone-tolerance')}),
        ('sweep', 'Level Sweep', 'gegl:levels', level_sweep_props(config)),
        ('pca', 'Principal Component 2', 'forensics:pca', {'component': 2}),
        ('pca', 'Principal Component 3', 'forensics:pca', {'component': 3}),
        ('hsv', 'HSV Hue', 'gegl:component-extract', {'component': 'hue'}),
        ('hsv', 'HSV Saturation', 'gegl:component-extract', {'component': 'hsv-s'}),
        ('hsv', 'HSV Value', 'gegl:component-extract', {'component': 'hsv-v'}),
        ('lab', 'LAB L', 'gegl:component-extract', {'component': 'lab-l'}),
        ('lab', 'LAB A', 'gegl:component-extract', {'component': 'lab-a'}),
        ('lab', 'LAB B', 'gegl:component-extract', {'component': 'lab-b'}),
    ]


class WorkbenchError(Exception):
    pass


def workbench_groups(image):
    """the Forensics groups this plug-in added before"""
    return [l for l in image.get_layers()
            if l.is_group() and l.get_parasite(PARASITE) is not None]


def analyze(image, config):
    chosen = [a for a in analyses(config) if config.get_property(a[0])]
    if not chosen:
        raise WorkbenchError('Choose at least one analysis.')
    for switch, name, op, props in chosen:
        if not Gegl.has_operation(op):
            raise WorkbenchError(
                'The filter %s is not installed: install the forensics '
                'operations (see the README of gimp-forensics) and restart GIMP.' % op)

    image.undo_group_start()
    try:
        # the image as it looks, without earlier Forensics groups (which
        # stay hidden: the new analyses are on top)
        for g in workbench_groups(image):
            g.set_visible(False)
        base = Gimp.Layer.new_from_visible(image, image, 'image')

        group = Gimp.GroupLayer.new(image, GROUP_NAME)
        image.insert_layer(group, None, 0)
        group.attach_parasite(Gimp.Parasite.new(PARASITE, Gimp.PARASITE_PERSISTENT,
                                                b'1'))
        # bottom first, so that the first analysis ends up on top
        layers = []
        for switch, name, op, props in reversed(chosen):
            layer = base.copy()
            layer.set_name(name)
            image.insert_layer(layer, group, 0)
            f = Gimp.DrawableFilter.new(layer, op, name)
            cfg = f.get_config()
            for k, v in props.items():
                cfg.set_property(k, v)
            f.update()
            layer.append_filter(f)
            layers.append(layer)
        # only the top analysis visible
        for layer in layers:
            layer.set_visible(layer is layers[-1])
        base.delete()
        image.set_selected_layers([layers[-1]])
    finally:
        image.undo_group_end()
    return group


GimpUi = None


def show_dialog(procedure, config):
    global GimpUi
    gi.require_version('GimpUi', '3.0')
    from gi.repository import GimpUi as _GimpUi
    GimpUi = _GimpUi
    GimpUi.init(PLUG_IN_BINARY)
    dialog = GimpUi.ProcedureDialog.new(procedure, config, 'Forensics Workbench')
    frames = []
    for switch, settings in (('ela', ['ela-quality', 'ela-scale']),
                             ('ghost', ['ghost-quality']),
                             ('noise', ['noise-amplitude']),
                             ('gradient', []),
                             ('clone', ['clone-tolerance']),
                             ('sweep', ['sweep-position', 'sweep-width']),
                             ('pca', []), ('hsv', []), ('lab', [])):
        if settings:
            dialog.fill_box(switch + '-box', settings)
            dialog.fill_frame(switch + '-frame', switch, False, switch + '-box')
            frames.append(switch + '-frame')
        else:
            frames.append(switch)
    dialog.get_label('about', 'Each analysis becomes a layer of the Forensics '
                     'group with its filter, which can be edited later. The '
                     'results are indicators, not proof.', False, True)
    dialog.fill(['about'] + frames)
    try:
        return dialog.run()
    finally:
        dialog.destroy()


def run(procedure, run_mode, image, drawables, config, data):
    try:
        if image.get_base_type() == Gimp.ImageBaseType.INDEXED:
            raise WorkbenchError('The Forensics Workbench works on RGB and grayscale '
                                 'images (Image > Mode).')
        if run_mode == Gimp.RunMode.INTERACTIVE and not show_dialog(procedure, config):
            return procedure.new_return_values(Gimp.PDBStatusType.CANCEL, None)
        analyze(image, config)
        Gimp.displays_flush()
        return procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, None)
    except WorkbenchError as e:
        if run_mode != Gimp.RunMode.NONINTERACTIVE:
            Gimp.message(str(e))
            return procedure.new_return_values(Gimp.PDBStatusType.CANCEL, None)
        return procedure.new_return_values(Gimp.PDBStatusType.CALLING_ERROR,
                                           GLib.Error(str(e)))
    except Exception as e:
        traceback.print_exc()
        return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                           GLib.Error('Forensics Workbench failed: %s' % e))


class Workbench(Gimp.PlugIn):

    def do_set_i18n(self, name):
        return False

    def do_query_procedures(self):
        return [PROC_NAME]

    def do_create_procedure(self, name):
        Gegl.init(None)
        p = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, run, None)
        p.set_image_types('RGB*, GRAY*')
        p.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE |
                               Gimp.ProcedureSensitivityMask.DRAWABLES |
                               Gimp.ProcedureSensitivityMask.NO_DRAWABLES)
        p.set_menu_label('_Analyze Image...')
        p.add_menu_path(MENU)
        p.set_documentation(
            'Adds forensic analyses of the image as layers with filters',
            'Adds a layer group "Forensics" at the top of the image with one copy '
            'of the image as it looks per analysis, each with its analysis as a '
            'non-destructive filter (Error Level Analysis, JPEG Ghost, Noise '
            'Analysis, Luminance Gradient, Clone Detection, Principal Components, '
            'a level sweep, the HSV and LAB channels). Only the top analysis is '
            'visible. The image\'s own layers are not changed. The analyses are '
            'indicators, not proof.', name)
        p.set_attribution('David', 'David', '2026')
        rw = GObject.ParamFlags.READWRITE
        p.add_boolean_argument('ela', 'Error _Level Analysis',
                               'Saves the image again as a JPEG and shows what changed', True, rw)
        p.add_int_argument('ela-quality', 'JPEG _quality', 'The quality of the JPEG copy',
                           0, 100, 90, rw)
        p.add_double_argument('ela-scale', 'Error _scale', 'How much the error is amplified',
                              0.0, 100.0, 20.0, rw)
        p.add_boolean_argument('ghost', '_JPEG Ghost',
                               'Where the image differs least from a JPEG copy at a quality',
                               True, rw)
        p.add_int_argument('ghost-quality', 'Ghost qualit_y',
                           'The quality at which a region saved before shows up dark',
                           0, 100, 70, rw)
        p.add_boolean_argument('noise', '_Noise Analysis', 'The noise of the image', True, rw)
        p.add_double_argument('noise-amplitude', 'Noise _amplitude',
                              'How much the noise is amplified', 0.0, 1000.0, 10.0, rw)
        p.add_boolean_argument('gradient', 'Luminance _Gradient',
                               'How the brightness changes, as a color', True, rw)
        p.add_boolean_argument('clone', 'Clone _Detection',
                               'Parts copied to another place of the image', True, rw)
        p.add_double_argument('clone-tolerance', 'Clone _tolerance',
                              'How different a copy may be, in levels', 0.25, 20.0, 2.0, rw)
        p.add_boolean_argument('sweep', 'Level S_weep',
                               'The contrast of a narrow band of brightness, magnified',
                               True, rw)
        p.add_double_argument('sweep-position', 'Sweep _position',
                              'The middle of the band (0 black, 1 white)',
                              0.0, 1.0, 0.5, rw)
        p.add_double_argument('sweep-width', 'Sweep _width',
                              'The width of the band', 0.01, 1.0, 0.125, rw)
        p.add_boolean_argument('pca', '_Principal Components',
                               'The image along its second and third color components',
                               True, rw)
        p.add_boolean_argument('hsv', '_HSV channels', 'Hue, saturation and value', False, rw)
        p.add_boolean_argument('lab', 'LA_B channels', 'L, a and b of CIE LAB', False, rw)
        return p


if __name__ == '__main__':
    Gimp.main(Workbench.__gtype__, sys.argv)
