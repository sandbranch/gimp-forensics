#!/usr/bin/env python3
# Downloads the C2PA library the plug-in uses, once, into vendor/ next to
# this script: the c2pa-python bindings (a pure Python ctypes wrapper) and
# libc2pa_c.so, the C API of c2pa-rs, the C2PA reference implementation,
# from the pinned wheel on PyPI. The download is checked against a fixed
# SHA-256; the plug-in itself never goes online.
#
#   plug-ins/content-credentials/fetch-deps.py          fetch if missing
#   plug-ins/content-credentials/fetch-deps.py --force  fetch again
#   plug-ins/content-credentials/fetch-deps.py --check  exit 1 if missing
#
# c2pa-python and c2pa-rs are licensed MIT OR Apache-2.0 (both licence
# texts are saved next to them); libc2pa_c.so links its Rust dependencies
# statically, under their own licences (listed in the SBOM of the c2pa-rs
# release). Only Linux x86_64 and aarch64 are pinned here.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import io
import os
import platform
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR = os.path.join(HERE, 'vendor')
VERSION = '0.37.12'          # c2pa-python, with c2pa-rs 0.91.0
PYPI = 'https://files.pythonhosted.org/packages/'
WHEELS = {
    'x86_64': ('cf/5d/828d12bf83d2352531e4084df25673005d9d2774a69444423488a7e57058/'
               'c2pa_python-0.37.12-py3-none-manylinux_2_28_x86_64.whl',
               '1bb5607529b6acb4134e10c07dbc030b566dba9e66da7f9fe6265a77733191f6'),
    'aarch64': ('88/b1/138f61133c37fe34226b2596e2c763ecd3c5788766f82a2e48e41faa01ae/'
                'c2pa_python-0.37.12-py3-none-manylinux_2_28_aarch64.whl',
                'a3883b5b8d1c1f27a7e85cea07ea3b38b331427ba3387ac56680e32fe6aeb302'),
}
LICENCES = {
    'LICENSE-MIT': '89375a50de90d2dcaa04406086da832ad452ebcaf6ab402ef3d51b8401a67c71',
    'LICENSE-APACHE': '86bdd5dafab77451044b6fd6d2efab23e3410ce658eb097f04c04f4f54aed62f',
}
LICENCE_URL = 'https://raw.githubusercontent.com/contentauth/c2pa-python/v%s/' % VERSION
# the files taken from the wheel (not the 30 MB static .rlib, nor build.py,
# which is for building and needs requests)
KEEP = ('c2pa/__init__.py', 'c2pa/c2pa.py', 'c2pa/lib.py', 'c2pa/libs/libc2pa_c.so')


def get(url, sha256):
    print('fetching', url, file=sys.stderr)
    with urllib.request.urlopen(url, timeout=120) as r:
        data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != sha256:
        sys.exit('fetch-deps.py: %s has SHA-256 %s, expected %s' % (url, digest, sha256))
    return data


def present():
    return all(os.path.isfile(os.path.join(VENDOR, k)) for k in KEEP) and \
        os.path.isfile(os.path.join(VENDOR, 'VERSION'))


def main():
    args = sys.argv[1:]
    if '--check' in args:
        sys.exit(0 if present() else 1)
    if present() and '--force' not in args:
        with open(os.path.join(VENDOR, 'VERSION')) as f:
            print('vendor/ has c2pa-python', f.read().strip())
        return
    machine = platform.machine().lower()
    machine = {'amd64': 'x86_64', 'arm64': 'aarch64'}.get(machine, machine)
    if not sys.platform.startswith('linux') or machine not in WHEELS:
        sys.exit('fetch-deps.py: only Linux x86_64 and aarch64 are pinned (this is %s %s); '
                 'get c2pa-python %s for your system from PyPI and put its c2pa folder into %s'
                 % (sys.platform, machine, VERSION, VENDOR))
    path, sha256 = WHEELS[machine]
    wheel = zipfile.ZipFile(io.BytesIO(get(PYPI + path, sha256)))
    tmp = VENDOR + '.new'
    shutil.rmtree(tmp, ignore_errors=True)
    for name in KEEP:
        target = os.path.join(tmp, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as f:
            f.write(wheel.read(name))
        os.chmod(target, 0o644)
    for name, digest in LICENCES.items():
        with open(os.path.join(tmp, 'c2pa', name), 'wb') as f:
            f.write(get(LICENCE_URL + name, digest))
    with open(os.path.join(tmp, 'VERSION'), 'w') as f:
        f.write('%s (%s, sha256 %s)\n' % (VERSION, os.path.basename(path), sha256))
    shutil.rmtree(VENDOR, ignore_errors=True)
    os.rename(tmp, VENDOR)
    print('vendor/ has c2pa-python', VERSION)


if __name__ == '__main__':
    main()
