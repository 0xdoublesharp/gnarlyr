#!/usr/bin/env python3
"""Create a new offline ECHO analysis run; all generated files go to Scratch."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
from echo_image import Image
from check_analysis import check


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=Path('/Volumes/Scratch/SnowskyEcho/data/stock/ECHOV200.IMG'))
    p.add_argument('--run-name',default=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    p.add_argument('--ghidra',type=Path,default=Path('/Volumes/Scratch/SnowskyEcho/tools/ghidra_12.1.4_PUBLIC'))
    p.add_argument('--java-home',type=Path,default=Path('/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home'))
    args=p.parse_args()
    if not args.run_name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.run_name):
        p.error('Use an alphanumeric run name with optional hyphens/underscores')
    scratch=Path('/Volumes/Scratch')
    if not os.path.ismount(scratch): p.error('Scratch volume is not mounted')
    root=scratch/'SnowskyEcho'
    build=root/'build'/args.run_name
    reports=root/'reports'/args.run_name
    temporary=root/'tmp'/args.run_name
    for path in (build,reports,temporary):
        if path.exists(): p.error(f'Run path already exists: {path}')
    image=Image(args.image.read_bytes())
    elf,mapping=image.analysis_elf([0,1])
    for path in (build,reports,temporary): path.mkdir(parents=True)
    (build/'projects').mkdir()
    (temporary/'java-home').mkdir()
    target=build/'echo-main-overlay001.elf'
    target.write_bytes(elf)
    target.with_suffix('.map.json').write_text(json.dumps(mapping,indent=2)+'\n')
    (reports/'container-map.json').write_text(json.dumps(image.summary(),indent=2)+'\n')
    scripts=Path(__file__).resolve().parents[1]/'ghidra'
    env=os.environ.copy()
    env.update(JAVA_HOME=str(args.java_home), TMPDIR=str(temporary), GHIDRA_HEADLESS_MAXMEM='1536M')
    env['JAVA_TOOL_OPTIONS']=' '.join([env.get('JAVA_TOOL_OPTIONS',''),
        '-Duser.home='+str(temporary/'java-home'),'-Djava.io.tmpdir='+str(temporary),'-XX:ActiveProcessorCount=4'])
    command=[str(args.ghidra/'support/analyzeHeadless'),str(build/'projects'),'echo-v200',
             '-import',str(target),'-processor','ARM:LE:32:Cortex','-cspec','default',
             '-scriptPath',str(scripts),'-preScript','PrepareEcho.java','0x0307e8c0',
             '-postScript','ExportEcho.java',str(reports/'ghidra'),'0x030622ca','0x0305ff70',
             '0x0307e8c0','0x03056388','0x030563c2','0x030527d4',
             '-analysisTimeoutPerFile','180','-max-cpu','4']
    with (reports/'ghidra-run.log').open('w') as log:
        subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    analysis=json.loads((reports/'ghidra/analysis.json').read_text())
    regions,functions=check(mapping,analysis)
    print(f'PASS: {regions} regions loaded byte-for-byte; {functions} selected functions decompiled in Thumb mode')
    print('Ghidra function candidates:',analysis['function_count'])
    print('Project:',build/'projects/echo-v200.gpr')
    print('Report:',reports/'ghidra/analysis.json')

if __name__=='__main__': main()
