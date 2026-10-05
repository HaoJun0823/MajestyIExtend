# -*- coding: utf-8 -*-
"""用原版 CAM 文件验证 解包->打包 字节级往返。

遍历指定目录下所有 .cam 文件，对每个文件：
  1. 解包到临时目录
  2. 重新打包为字节流
  3. 与原文件做 SHA-256 比对
  4. 报告 PASS / FAIL

用法:
  python verify.py [--src <update根目录>] [--tmp <临时目录>]
"""
import os
import sys
import tempfile
import argparse
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packlib as P

DEFAULT_SRC = r'I:\SteamLibrary\steamapps\common\Majesty HD\update'


def collect_cams(src):
    files = []
    for root, dirs, fs in os.walk(src):
        for f in fs:
            ext = f.lower()
            if ext.endswith('.cam') or (ext.endswith('.dat') and f.startswith('UIData')):
                files.append(os.path.join(root, f))
    return sorted(files)


def verify_file(infile, tmp):
    data = open(infile, 'rb').read()
    if not data.startswith(b'CYLBPC'):
        return 'skip', False, len(data)
    outdir = os.path.join(tmp, 'u')
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    P.unpack_to_dir(infile, outdir)
    reb = P.pack_from_dir(outdir)
    ok = reb == data
    return 'cam', ok, len(data)


def main():
    ap = argparse.ArgumentParser(description='验证 CAM 解包/打包往返')
    ap.add_argument('--src', default=DEFAULT_SRC, help='update 根目录')
    ap.add_argument('--tmp', default=None, help='临时目录')
    args = ap.parse_args()

    files = collect_cams(args.src)
    if not files:
        print('未在 %s 找到 .cam 文件' % args.src)
        return 1

    tmp = args.tmp or tempfile.mkdtemp(prefix='cam_verify_')

    fails = []
    print('%-52s %8s  %s' % ('file', 'bytes', 'result'))
    print('-' * 80)
    for f in files:
        typ, ok, sz = verify_file(f, tmp)
        rel = os.path.relpath(f, args.src)
        if ok:
            print('%-52s %8d  PASS' % (rel, sz))
        else:
            print('%-52s %8d  FAIL' % (rel, sz))
            fails.append(rel)
    print('-' * 80)
    print('总计 %d 个文件；失败 %d 个' % (len(files), len(fails)))

    try:
        shutil.rmtree(tmp)
    except Exception:
        pass
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
