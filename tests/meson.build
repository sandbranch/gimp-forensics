# pass/fail checks of each operation: tests/check.sh, or meson test -C build
check_args = ['-DGEGL_PLUGINSDIR="@0@"'.format(
  gegl_dep.get_variable(pkgconfig: 'pluginsdir'))]

checks = {
  'error-level': 'check-error-level.c',
  'jpeg-ghost': 'check-jpeg-ghost.c',
  'noise': 'check-noise.c',
  'luminance-gradient': 'check-luminance-gradient.c',
  'clone-detect': 'check-clone-detect.c',
  'pca': 'check-pca.c',
}

foreach op, source : checks
  exe = executable('check-' + op, source,
    include_directories: include_directories('../operations'),
    dependencies: [gegl_dep, jpeg_dep, math_dep],
    c_args: check_args,
    install: false,
  )
  test(op, exe, args: [modules[op]], timeout: 900)
endforeach

# how long the operations take on a 24 megapixel image: tests/bench.sh
executable('bench', 'bench.c',
  include_directories: include_directories('../operations'),
  dependencies: [gegl_dep, jpeg_dep, math_dep],
  c_args: check_args,
  install: false,
)
