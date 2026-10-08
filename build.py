#!/usr/bin/env python3
"""Build a native PE32 GUI and DLL. Requires MinGW-w64 x86 GCC, Python 3.9+."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent
VERSION = '0.1.0-candidate.4'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--cc', default='i686-w64-mingw32-gcc')
    p.add_argument('--cxx', default='i686-w64-mingw32-g++')
    a = p.parse_args()
    if subprocess.check_output([a.cxx, '-dumpmachine'], text=True).strip() != 'i686-w64-mingw32':
        raise SystemExit('Use the i686-w64-mingw32 (32-bit Windows) compiler')
    vendor = ROOT / 'vendor/minhook'
    for f in json.loads((vendor/'SOURCE.json').read_text())['files']:
        if hashlib.sha256((vendor/f['path']).read_bytes()).hexdigest() != f['sha256']:
            raise SystemExit('MinHook source fingerprint mismatch: '+f['path'])
    out = ROOT/'build'/('UNI2Blacklist-'+VERSION+'-win32')
    out.mkdir(parents=True, exist_ok=True)
    obj = ROOT/'build/obj';obj.mkdir(exist_ok=True)
    common = ['-O2','-Wall','-Wextra','-Werror','-finput-charset=UTF-8',
              '-I'+str(ROOT/'include'),'-I'+str(vendor/'include')]
    objects = []
    def run(argv):
        subprocess.run([str(x) for x in argv], cwd=ROOT, check=True)
    for name in ['buffer','hook','trampoline','hde/hde32']:
        target=obj/(name.replace('/','_')+'.o');objects.append(target)
        run([a.cc,*common,'-Wno-unused-parameter','-c',vendor/'src'/(name+'.c'),'-o',target])
    cpp = [a.cxx,*common,'-std=c++17','-static','-static-libgcc','-static-libstdc++']
    run([*cpp,'-shared','-Wl,--kill-at',ROOT/'src/runtime.cpp',*objects,'-ladvapi32','-o',out/'uni2-blacklist.dll'])
    run([*cpp,'-mwindows','-municode',ROOT/'src/gui.cpp',ROOT/'src/injector.cpp',ROOT/'src/settings.cpp',
         '-lcomctl32','-lcomdlg32','-lshell32','-ladvapi32','-o',out/'UNI2Blacklist.exe'])
    for name in ['README.zh-CN.md','LICENSE']:
        if (ROOT/name).exists():shutil.copy2(ROOT/name,out/name)
    shutil.copy2(vendor/'LICENSE.txt',out/'MinHook-LICENSE.txt')
    (out/'docs').mkdir(exist_ok=True)
    for name in ['VALIDATION.zh-CN.md','NATIVE_CONTRACT.zh-CN.md','HOOK_FIX.zh-CN.md','QUICK_MATCH_CRASH.zh-CN.md','EXE_UPDATE.zh-CN.md']:
        if (ROOT/'docs'/name).exists():shutil.copy2(ROOT/'docs'/name,out/'docs'/name)
    receipt={f.relative_to(out).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(out.rglob('*')) if f.is_file() and f.name!='SHA256.json'}
    (out/'SHA256.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(out)

if __name__=='__main__':main()
