#!/usr/bin/env python3
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('binary', type=Path, nargs='?')
args = parser.parse_args()
root, build = args.source, args.build
config=(build/'config.mk').read_text()
header=(build/'config.h').read_text()
engine_definitions={name.upper() for p in (root/'engines').glob('*/configure.engine') for name in re.findall(r'^add_engine\s+(\w+)',p.read_text(),re.M)}
enabled=set(re.findall(r'^ENABLE_(\w+)\s*=\s*(?:STATIC_PLUGIN|1)\s*$',config,re.M))
unsupported={'MACS2':'Requires ImGui integration unavailable in the iOS backend', 'WATCHMAKER':'Requires desktop OpenGL classic, unavailable with the iOS GLES backend'}
missing=engine_definitions-enabled
assert missing==set(unsupported), 'Unexpected disabled engines: '+repr(sorted(missing))
plugins=set(re.findall(r'LINK_PLUGIN\((\w+)\)',(build/'engines/plugins_table.h').read_text())) & enabled
detectors=set(re.findall(r'LINK_PLUGIN\((\w+)\)',(build/'engines/detection_table.h').read_text()))
features=['JPEG','MAD','VORBIS','FLAC','THEORADEC','FAAD','MPEG2','A52','MPCDEC','MIKMOD','VPX','FRIBIDI','FLUIDSYNTH','LIBCURL','SDL_NET','PNG','FREETYPE2','GIF','MT32EMU','TINYGL']
for name in features:
    assert re.search(r'^#define USE_'+name+r'\b',header,re.M), 'Missing full dependency: '+name
if args.binary is None:
    print('PASS: full configuration:',len(engine_definitions-missing),'engine/subengine entries;',len(plugins),'implementations;',len(detectors),'detectors;',len(features),'required features')
    print('Platform exclusions:',unsupported)
    sys.exit(0)
nm=Path(os.environ['IOS_TOOLCHAIN_BIN'])/'nm'
symbols=set(subprocess.check_output([str(nm),'-j',str(args.binary)],text=True).split())
factories={match.group(1) for symbol in symbols if (match := re.fullmatch(r'__Z\d+g_(\w+)_getObjectv',symbol))}
assert plugins <= factories, 'Missing implementations: '+repr(sorted(plugins-factories))
assert detectors <= factories, 'Missing detectors: '+repr(sorted(detectors-factories))
print('PASS:',len(engine_definitions-missing),'engine/subengine declarations enabled;',len(plugins),'engine implementations and',len(detectors),'detectors linked; full dependency features enabled')
print('Platform exclusions:',unsupported)
(build/'full-build-report.json').write_text(json.dumps({'engines':sorted(engine_definitions-missing),'unsupported':unsupported,'plugins':sorted(plugins),'detectors':sorted(detectors),'features':features},indent=2)+'\n')
