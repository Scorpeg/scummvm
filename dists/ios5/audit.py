#!/usr/bin/env python3
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
binary = Path(sys.argv[1])
data = binary.read_bytes()
magic, cpu, subtype, kind, count, size, flags = struct.unpack_from('<7I', data)
assert (magic, cpu, subtype & 0xffffff, kind) == (0xfeedface, 12, 9, 2), 'Expected ARMv7 executable'
allowed = {'libobjc.A.dylib', 'UIKit', 'CoreGraphics', 'OpenGLES', 'QuartzCore',
           'CoreAudio', 'CoreVideo', 'SystemConfiguration', 'CoreFoundation', 'Foundation', 'AudioToolbox', 'libSystem.B.dylib',
           'libz.1.dylib', 'libstdc++.6.dylib', 'libc++.1.dylib'}
libs, min_os, signed = [], None, False
pos = 28
for _ in range(count):
    cmd, length = struct.unpack_from('<II', data, pos)
    assert length >= 8 and pos + length <= len(data)
    if cmd in (12, 0x80000018):
        nameoff = struct.unpack_from('<I', data, pos + 8)[0]
        path = data[pos + nameoff:pos + length].split(b'\0', 1)[0].decode()
        assert Path(path).name in allowed and path.startswith(('/System/Library/', '/usr/lib/')), path
        libs.append(path)
    elif cmd == 0x25:
        min_os = struct.unpack_from('<I', data, pos + 8)[0]
    elif cmd == 0x1d:
        offset, signature_size = struct.unpack_from('<II', data, pos + 8)
        signed = signature_size > 0 and offset + signature_size <= len(data)
        assert struct.unpack_from('>I', data, offset)[0] == 0xfade0cc0, 'Invalid signature superblob'

        entries = struct.unpack_from('>I', data, offset + 8)[0]
        sha1 = False
        for entry in range(entries):
            slot, off = struct.unpack_from('>II', data, offset + 12 + 8 * entry)
            if struct.unpack_from('>I', data, offset + off)[0] == 0xfade0c02:
                sha1 |= data[offset + off + 37] == 1
        assert sha1, 'Legacy device requires SHA1 code directory'
    pos += length
assert min_os == 0x50100, hex(min_os or 0)
assert signed, 'Missing jailbreak pseudo-signature'
nm = str(Path(os.environ['IOS_TOOLCHAIN_BIN']) / 'nm')
sdk = Path(os.environ['IOS_SDK_ROOT'])
imports = set(subprocess.check_output([nm, '-uj', str(binary)], text=True).split())
exports = set()
pending = list(libs)
visited = set()
otool = str(Path(os.environ['IOS_TOOLCHAIN_BIN']) / 'otool')
while pending:
    lib = pending.pop()
    if lib in visited: continue
    visited.add(lib)
    stub = sdk / lib.lstrip('/')
    assert stub.is_file(), f'Missing SDK stub: {stub}'
    exports.update(subprocess.check_output([nm, '-arch', 'armv7', '-gjU', str(stub)], text=True).split())
    commands = subprocess.check_output([otool, '-arch', 'armv7', '-l', str(stub)], text=True)
    for block in commands.split('Load command '):
        if re.search(r'cmd LC_REEXPORT_DYLIB\b', block):
            pending.append(re.search(r'\bname (.*?) \(offset', block).group(1))
missing = imports - exports
assert not missing, 'Imports absent from linked SDK5.1 libraries: ' + ', '.join(sorted(missing))
assert not any('__atomic_' in i for i in imports)
print(f'PASS: ARMv7, minimum iOS 5.1, SHA1 code directory; {len(imports)} imports found in {len(libs)} SDK libraries')
for lib in libs: print('  ' + lib)
