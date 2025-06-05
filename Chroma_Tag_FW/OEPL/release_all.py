#!/usr/bin/python3
import sys
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
}

oepl_repo='../../../OpenEPaperLink/'
tag_ota_versions='binaries/Tag/tagotaversions.json'
js=json.load(open(oepl_repo + tag_ota_versions,'r'))

shell = os.environ.get('SHELL')
for key in builds.keys():
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
    js[0][values['hw_id']][values['md5']] = md5sum
    js[0][values['hw_id']][values['version']] = info[0]["FW_VER"]
    json.dump(js,open(oepl_repo + tag_ota_versions,'w'),indent=2)

cmd_line = [ f'{shell}', '-c',f'cd {oepl_repo};git add {tag_ota_versions}']
subprocess.run(cmd_line)
cmd_line = [ f'{shell}', '-c',f'cd {oepl_repo};git status -uno']
subprocess.run(cmd_line)



