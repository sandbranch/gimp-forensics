# Runs inside GIMP on a Broadway display (gui/start.sh): opens CC_GUI_FILE
# in a window and runs Image > Forensics > Content Credentials...
# interactively. When the dialog is closed, writes the status of the
# procedure and the report's status to CC_OUT/result.txt, and quits GIMP.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os

import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

out = os.environ['CC_OUT']
image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE,
                       Gio.File.new_for_path(os.environ['CC_GUI_FILE']))
display = Gimp.Display.new(image)
Gimp.displays_flush()
pdb = Gimp.get_pdb()
proc = pdb.lookup_procedure('plug-in-content-credentials')
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', image.get_selected_drawables())
values = proc.run(config)
status = values.index(0)
report = json.loads(values.index(1)) if status == Gimp.PDBStatusType.SUCCESS else {}
with open(os.path.join(out, 'result.txt.tmp'), 'w') as f:
    f.write('status %s\nreport %s\n' % (status.value_nick, report.get('status')))
os.rename(os.path.join(out, 'result.txt.tmp'), os.path.join(out, 'result.txt'))

quit_proc = pdb.lookup_procedure('gimp-quit')
quit_config = quit_proc.create_config()
quit_config.set_property('force', True)
quit_proc.run(quit_config)
