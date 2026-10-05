# -*- coding: utf-8 -*-
"""解包 Majesty HD CAM 文件。

用法:
  python unpack.py <输入.cam> [输出目录]
      -> 把单个 CAM 解包到 输出目录（默认 <输入>.unpacked）
  python unpack.py --dir <输入目录> <输出根目录>
      -> 递归遍历输入目录下所有 .cam 文件，分别解包

可选:
  --json   额外导出 STRT section 的 .json（解析后的字符串列表，
           仅供阅读/编辑参考，重打包仍以 .bin 原字节为准）
"""
import os
import sys
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packlib as P


def unpack_file(infile, outdir, export_json):
    m = P.unpack_to_dir(infile, outdir, export_strt_json=export_json)
    sec_summary = ', '.join(
        '%s(%d)' % (s['ext'], len(s['files'])) for s in m['sections']
    )
    print('%-30s -> %s  [%s]' % (
        os.path.basename(infile), outdir, sec_summary))


def main():
    ap = argparse.ArgumentParser(description='解包 Majesty HD CAM 文件')
    ap.add_argument('input', help='输入 .cam 文件或目录')
    ap.add_argument('output', nargs='?', default=None, help='输出目录')
    ap.add_argument('--dir', action='store_true', help='递归处理目录下所有 .cam')
    ap.add_argument('--json', action='store_true', help='额外导出 STRT .json')
    args = ap.parse_args()

    if args.dir:
        indir = args.input
        outroot = args.output or (indir.rstrip('/\\') + '.unpacked')
        n = 0
        for root, dirs, files in os.walk(indir):
            for f in sorted(files):
                if f.lower().endswith('.cam'):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, indir)
                    outdir = os.path.join(outroot, rel + '.unpacked')
                    unpack_file(full, outdir, args.json)
                    n += 1
        print('\n完成：%d 个 CAM 已解包到 %s' % (n, outroot))
    else:
        infile = args.input
        outdir = args.output or (infile + '.unpacked')
        unpack_file(infile, outdir, args.json)
        print('\n完成：%s' % outdir)


if __name__ == '__main__':
    main()
