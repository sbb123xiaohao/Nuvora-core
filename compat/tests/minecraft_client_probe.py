#!/usr/bin/env python3
"""Record an official client's actual Nuvora launch; never infer gameplay.

This is a diagnostic probe. Its JSON always leaves world/input/save acceptance
unverified; use --require-playable to return failure while that is unresolved.
"""
import argparse
import json
import pathlib
import re
import sys
import time

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from test import VM


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build/minecraft-probe/client.log')
    parser.add_argument('--timeout',type=int,default=900)
    parser.add_argument('--trace',action='store_true')
    parser.add_argument('--uefi',action='store_true')
    parser.add_argument('--require-playable',action='store_true')
    options=parser.parse_args()
    if not options.disk.is_file():parser.error('Prepare a private disk with compat/tools/prepare_minecraft_probe.py first')
    if not 30<=options.timeout<=3600:parser.error('Timeout must be between 30 and 3600 seconds')
    provenance=json.loads(options.disk.with_suffix('.provenance.json').read_text())
    if provenance.get('version') not in ('1.20.1','1.21.1'):parser.error('Expected a pinned client probe disk')
    options.output.parent.mkdir(parents=True,exist_ok=True)
    started=time.monotonic();vm=None;out='';recovery=False;failure=None;original_pump=None
    try:
        vm=VM('minecraft-client-'+provenance['version'],memory=1024,boot_timeout=45,uefi=options.uefi,
              hardware=['-drive',f'file={options.disk},format=raw,if=none,id=mcdisk',
                        '-device','nvme,drive=mcdisk,serial=MC-CLIENT-PROBE'])
        with options.output.open('wb') as live:
            last=0;original_pump=vm.pump
            def pump(*args,**kwargs):
                nonlocal last
                original_pump(*args,**kwargs);live.write(vm.output[last:]);live.flush();last=len(vm.output)
            vm.pump=pump
            try:
                out=vm.send('compat --jit '+('--trace ' if options.trace else '')+
                            '/home/jdk/bin/java @/home/minecraft/demo.args',timeout=options.timeout)
                vm.send('horizon','filesystem nodes:',timeout=20);recovery=True
            except AssertionError:
                # VM.wait_for includes the entire trace in its exception. The
                # raw log already holds it; keep the report bounded.
                failure='Guest did not return to a healthy native shell within the probe deadline.'
                out=vm.text()
    finally:
        if vm:
            if original_pump is not None:vm.pump=original_pump
            vm.close(normal=False)
    status=re.search(r'exited (\d+)',out)
    crash=re.search(r'Description: ([^\n]+)\n\s*\n([^\n]+)',out)
    report={'version':provenance['version'],'elapsed_seconds':round(time.monotonic()-started,2),
            'guest_exit_status':int(status[1]) if status else None,'native_shell_recovered':recovery,
            'markers':[value for value in ('net.minecraft.client.main.Main','Render thread','LWJGL','GLFW',
                       'UnknownHostException','UnsatisfiedLinkError','OutOfMemoryError') if value in out],
            'failure':failure,'crash_description':crash[1] if crash else None,
            'crash_exception':crash[2][:512] if crash else None,'playable':False,
            'acceptance':{stage:'not verified' for stage in ('world','rendering','input','save_reload')},
            'note':'Real guest output recorded. Client initialization/exit is not a gameplay acceptance.'}
    options.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    return 2 if failure or options.require_playable else 0


if __name__=='__main__':raise SystemExit(main())
