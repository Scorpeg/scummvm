#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tarfile
import tempfile

package = Path(sys.argv[1]).resolve()
members = subprocess.check_output(['ar', 't', str(package)], text=True).splitlines()
assert members == ['debian-binary', 'control.tar.gz', 'data.tar.gz'], members
with tempfile.TemporaryDirectory() as temp:
    stage = Path(temp)
    subprocess.run(['dpkg-deb', '-x', str(package), temp], check=True)
    app = stage/'Applications/ScummVM-iOS5.app'
    info = plistlib.loads((app/'Info.plist').read_bytes())
    assert info['CFBundleIdentifier'] == 'org.scummvm.ios5'
    assert info['MinimumOSVersion'] == '5.1'
    assert info['CFBundleDisplayName'] == 'ScummVM'
    for name, digest in json.loads((app/'resources.sha256.json').read_text()).items():
        assert hashlib.sha256((app/name).read_bytes()).hexdigest() == digest, name
    lock = json.loads((app/'dependencies/full-dependencies.lock.json').read_text())
    assert hashlib.sha256((app/'cacert.pem').read_bytes()).hexdigest() == lock['sha256']['cacert.pem']
    for name in ['ScummVM', 'icon.png', 'icon-72.png', 'scummmodern.zip', 'fonts.dat', 'ios-help.zip']:
        assert (app/name).is_file(), name
    assert (stage/'usr/bin/scummvm-ios5-runtime-probe').is_file()
    assert not (stage/'var').exists(), 'Package must not own user data'
    subprocess.run(['dpkg-deb', '-e', str(package), str(stage/'control')], check=True)
    for name in ['postinst', 'postrm']:
        assert 'uicache' in (stage/'control'/name).read_text()
print('PASS: iOS5 Debian package, bundle identity, assets, CA and icon hooks')
