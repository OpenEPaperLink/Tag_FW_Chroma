#!/usr/bin/python3
import sys
import argparse
import os
import subprocess
import json
import hashlib

builds={'chroma74y':{'hw_id' : '80','version' : 'version','md5': 'md5'}
    ,'chroma74r':{'hw_id' : '80','version' : 'version_1','md5': 'md5_1'}
    ,'chroma29':{'hw_id' : '82','version': 'version','md5': 'md5'}
    ,'chroma29_8151':{'hw_id' : '82','version': 'version_1','md5': 'md5_1'}
    ,'chroma42':{'hw_id' : '83','version': 'version','md5': 'md5'}
    ,'chroma42_8176':{'hw_id' : '83','version': 'version_1','md5': 'md5_1'}
    ,'chroma60':{'hw_id' : '84','version': 'version','md5': 'md5'}
}

parser = argparse.ArgumentParser(
    description='Build release firmware and update tagotaversions.json.',
    epilog='Valid builds: ' + ', '.join(builds.keys()))
parser.add_argument('--all', action='store_true', help='build all targets')
parser.add_argument('targets', nargs='*', metavar='BUILD', help='build only the named target(s)')
args = parser.parse_args()

if args.all and args.targets:
    parser.error('specify either --all or named builds, not both')
if not args.all and not args.targets:
    parser.print_help()
    sys.exit(1)
unknown = [t for t in args.targets if t not in builds]
if unknown:
    parser.error(f"unknown build(s): {', '.join(unknown)}")
selected = list(builds.keys()) if args.all else args.targets

oepl_url='https://github.com/OpenEPaperLink/OpenEPaperLink.git'
releases_dir='releases'
oepl_repo=releases_dir + '/OpenEPaperLink/'
tag_ota_versions='binaries/Tag/tagotaversions.json'

# Create the releases directory with a shallow clone if it doesn't exist yet
if not os.path.isdir(oepl_repo):
    os.makedirs(releases_dir, exist_ok=True)
    subprocess.run(['git', 'clone', '--depth', '1', oepl_url, oepl_repo], check=True)

# Discard any local changes and get the latest upstream state
subprocess.run(['git', 'reset', '--hard'], cwd=oepl_repo, check=True)
subprocess.run(['git', 'pull'], cwd=oepl_repo, check=True)

js=json.load(open(oepl_repo + tag_ota_versions,'r'))

shell = os.environ.get('SHELL')
for key in selected:
    print(f"Building {key}")
    values=builds[key]
    print(f"hwid {values['hw_id']} md5 {values['md5']}")
    os.environ['BUILD']= key
    cmd_line = [ f'{shell}', '-c','make clean;make;make release']
    subprocess.run(cmd_line)
    info=json.loads(subprocess.run(['make','build_info'],stdout=subprocess.PIPE).stdout)
    h = hashlib.md5()
    with open(info[0]["OTA_BIN"], "rb") as f:
        h.update(f.read())
    md5sum = h.hexdigest()
    hw = js[0].get(values['hw_id'])
    if hw is None:
        # new boards have no entry yet: record the build type along with the md5sum
        hw = {'type': key + '_ota'}
        js[0][values['hw_id']] = hw
    hw[values['md5']] = md5sum
    hw[values['version']] = info[0]["FW_VER"]
    json.dump(js,open(oepl_repo + tag_ota_versions,'w'),indent=2)

cmd_line = [ f'{shell}', '-c',f'cd {oepl_repo};git add {tag_ota_versions}']
subprocess.run(cmd_line)
cmd_line = [ f'{shell}', '-c',f'cd {oepl_repo};git status -uno']
subprocess.run(cmd_line)
