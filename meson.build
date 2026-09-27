project('gimp-forensics', 'c',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['warning_level=1', 'buildtype=debugoptimized'],
)

gegl_dep = dependency('gegl-0.4', version: '>= 0.4.62')
jpeg_dep = dependency('libjpeg')
math_dep = meson.get_compiler('c').find_library('m', required: false)

# GEGL loads user operations from $XDG_DATA_HOME/gegl-0.4/plug-ins, which is
# ~/.local/share/gegl-0.4/plug-ins, or for the Flatpak version of GIMP
# ~/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins.
moduledir = get_option('moduledir')
if moduledir == ''
  moduledir = gegl_dep.get_variable(pkgconfig: 'pluginsdir')
endif

# each operation is a module of its own (Filters > Forensics in GIMP)
operations = {
  'error-level': ['operations/error-level.c'],
  'jpeg-ghost': ['operations/jpeg-ghost.c'],
  'noise': ['operations/noise.c'],
  'luminance-gradient': ['operations/luminance-gradient.c'],
  'clone-detect': ['operations/clone-detect.c'],
  'pca': ['operations/pca.c'],
  'bit-plane': ['operations/bit-plane.c'],
  'minmax': ['operations/minmax.c'],
  'wavelet-noise': ['operations/wavelet-noise.c'],
  'echo': ['operations/echo.c'],
  'median-detect': ['operations/median-detect.c'],
  'resampling': ['operations/resampling.c'],
}

modules = {}
foreach op, sources : operations
  modules += { op: shared_module(op,
    sources,
    name_prefix: '',
    include_directories: 'operations',
    dependencies: [gegl_dep, jpeg_dep, math_dep],
    c_args: ['-DGETTEXT_PACKAGE="gimp-forensics"'],
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: moduledir,
  )}
endforeach

subdir('plug-ins')
subdir('tests')
