#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import shlex
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--deb', action='store_true')
parser.add_argument('resources', nargs='*', type=Path)
args = parser.parse_args()
source, build = args.source.resolve(), args.build.resolve()
tools = Path(os.environ['IOS_TOOLCHAIN_BIN'])
prefix = Path(os.environ['PREFIX'])
support = source/'dists/ios5'
version = re.search(r'^#define SCUMMVM_VERSION "([^"]+)"', (source/'base/internal_version.h').read_text(), re.M).group(1)
package_version = version + '-ios5.' + os.environ.get('IOS5_PACKAGE_REVISION', '22')
full = os.environ.get('IOS5_REQUIRE_FULL') == '1'
update_url = os.environ.get('IOS5_UPDATE_URL', '')
update_key = os.environ.get('IOS5_UPDATE_PUBLIC_KEY', '')
if bool(update_url) != bool(update_key):
    raise RuntimeError('Set both IOS5_UPDATE_URL and IOS5_UPDATE_PUBLIC_KEY')
if update_url:
    parsed_url = urlsplit(update_url)
    if parsed_url.scheme != 'https' or not parsed_url.netloc or parsed_url.username or parsed_url.password or parsed_url.query or parsed_url.fragment:
        raise RuntimeError('Updater feed must be an HTTPS URL without credentials, query or fragment')
    update_url = update_url.rstrip('/') + '/'
    public_key = Path(update_key).read_bytes()
    subprocess.run(['openssl', 'rsa', '-pubin', '-noout'], input=public_key, check=True)
    package_version = version + '-ios5.' + str(int(time.time()))

def run(command):
    subprocess.run(list(map(str, command)), check=True, cwd=build)

if full:
    run(['python3', support/'check_full.py', '--source', source, '--build', build, build/'scummvm'])
run(shlex.split(os.environ['CXX']) + ['-std=c++11', '-O2', support/'runtime-probe.mm', '-DIOS5_FULL_DEPS', '-I'+str(prefix/'include'), '-L'+str(prefix/'lib'), '-lfluidsynth', '-lcurl', '-lssl', '-lcrypto', '-lz', '-lc++', '-framework', 'Foundation', '-lobjc', '-Wl,-no_data_const', '-o', build/'runtime-probe'])
run([tools/'ldid', '-Hsha1', '-S', build/'runtime-probe'])
run(['python3', support/'audit.py', build/'runtime-probe'])
if update_url:
    run(shlex.split(os.environ['CXX']) + ['-std=c++11', '-O2', support/'update-helper.cpp', '-I'+str(prefix/'include'), '-L'+str(prefix/'lib'), '-lcurl', '-lssl', '-lcrypto', '-lz', '-lc++', '-Wl,-no_data_const', '-o', build/'update-helper'])
    run([tools/'ldid', '-Hsha1', '-S', build/'update-helper'])
    run(['python3', support/'audit.py', build/'update-helper'])
with tempfile.TemporaryDirectory(prefix='ios5-package-', dir=build) as temporary:
    stage = Path(temporary)
    app = stage/'Applications/ScummVM-iOS5.app'
    app.mkdir(parents=True)
    shutil.copy2(build/'scummvm', app/'ScummVM')
    run([tools/'strip', '-S', app/'ScummVM'])
    run([tools/'ldid', '-Hsha1', '-S', app/'ScummVM'])
    run(['python3', support/'audit.py', app/'ScummVM'])
    info = plistlib.loads((support/'Info.plist').read_bytes())
    info['CFBundleShortVersionString'] = version
    info['CFBundleVersion'] = package_version
    (app/'Info.plist').write_bytes(plistlib.dumps(info, sort_keys=False))
    resources = args.resources + list((support/'assets').glob('*.png')) + [
        source/'dists/ios7/ios-help.zip', source/'COPYING', source/'COPYRIGHT',
        source/'LICENSES/COPYING.MPL', source/'LICENSES/COPYING.OFL',
        source/'dists/engine-data/fonts/fonts/README']
    manifest = {}
    for resource in resources:
        if not resource.is_absolute(): resource = build/resource
        digest = hashlib.sha256(resource.read_bytes()).hexdigest()
        name = resource.name
        if name in manifest and manifest[name] != digest:
            raise RuntimeError('Conflicting resource names: '+name)
        shutil.copy2(resource, app/name)
        manifest[name] = digest
    shutil.copytree(prefix/'share/ios5', app/'dependencies')
    shutil.copy2(prefix/'share/ios5/cacert.pem', app/'cacert.pem')
    if full: shutil.copy2(build/'full-build-report.json', app)
    (app/'resources.sha256.json').write_text(json.dumps(manifest, indent=2)+'\n')
    revision = os.environ.get('SCUMMVM_REVISION')
    if not revision:
        result = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True, capture_output=True)
        revision = result.stdout.strip() if result.returncode == 0 else 'source archive'
    (app/'build.json').write_text(json.dumps({'version': version, 'revision': revision, 'sdk': '5.1', 'arch': 'armv7', 'full': full, 'source_diff_sha256': os.environ.get('IOS5_SOURCE_DIFF_SHA256')}, indent=2)+'\n')
    destination = build/'ScummVM-iOS5.app'
    if destination.exists(): shutil.rmtree(destination)
    shutil.copytree(app, destination)
    if args.deb:
        debian = stage/'DEBIAN'
        debian.mkdir()
        for name in ['postinst', 'postrm']:
            shutil.copy2(support/name, debian/name)
            (debian/name).chmod(0o755)
        (debian/'control').write_text(f'Package: org.scummvm.ios5\nName: ScummVM\nVersion: {package_version}\nArchitecture: iphoneos-arm\nDescription: ScummVM for iOS 5.1 ARMv7\nMaintainer: ScummVM iOS5 port\nSection: Games\nPriority: optional\n')
        if update_url:
            config = stage/'etc/scummvm-ios5-updater'
            config.mkdir(parents=True)
            (config/'feed.url').write_text(update_url + '\n')
            (config/'public.pem').write_bytes(public_key)
            shutil.copy2(prefix/'share/ios5/cacert.pem', config/'cacert.pem')
            helper = stage/'usr/libexec/scummvm-ios5-update'
            helper.parent.mkdir(parents=True)
            shutil.copy2(build/'update-helper', helper)
            helper.chmod(0o4755)
        (stage/'usr/bin').mkdir(parents=True)
        shutil.copy2(build/'runtime-probe', stage/'usr/bin/scummvm-ios5-runtime-probe')
        output = build/f'scummvm-{version}-ios5.deb'
        run(['dpkg-deb', '--root-owner-group', '-Zgzip', '-z6', '--build', stage, output])
        run(['python3', support/'check_package.py', output])
    print('PASS: iOS5 bundle; resource entries:', len(manifest))
