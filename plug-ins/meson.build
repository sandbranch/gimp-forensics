# Python plug-ins: each in a folder of its own (as GIMP 3 wants them),
# installed into GIMP's plug-in folder when -Dplugindir is given. A
# plug-in folder here with a meson.build of its own (for example
# content-credentials) is built by that file instead.
plugindir = get_option('plugindir')
foreach plugin : ['forensics-workbench']
  if plugindir != ''
    install_data(plugin / plugin + '.py',
      install_dir: plugindir / plugin,
      install_mode: 'rwxr-xr-x')
  endif
endforeach
foreach sub : ['content-credentials']
  if import('fs').is_file(sub / 'meson.build')
    subdir(sub)
  endif
endforeach
