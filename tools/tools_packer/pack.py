# -*- coding: utf-8 -*-
"""打包 Majesty HD CAM 文件（与 unpack.py 配对）。

用法:
  python pack.py <解包目录> [输出.cam]
      -> 把单个解包目录重组为 CAM 文件
  python pack.py --dir <解包根目录> <输出根目录>
      -> 遍历解包根目录下所有 *.unpacked 目录，分别打包

重建严格依据 manifest.json，不做任何文本/编码转换，保证字节级一致。
"""
import os
import sys
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import packlib as P


def pack_dir(outdir, outfile):
    buf = P.pack_from_dir(outdir)
    os.makedirs(os.path.dirname(outfile) or '.', exist_ok=True)
    with open(outfile, 'wb') as fp:
        fp.write(buf)
    print('%-44s -> %s (%d B)' % (outdir, outfile, len(buf)))


def main():
    ap = argparse.ArgumentParser(description='打包 Majesty HD CAM 文件')
    ap.add_argument('input', help='解包目录或解包根目录')
    ap.add_argument('output', nargs='?', default=None, help='输出文件或目录')
    ap.add_argument('--dir', action='store_true', help='递归处理所有 .unpacked')
    args = ap.parse_args()

    if args.dir:
        indir = args.input
        outroot = args.output or indir
        n = 0
        for root, dirs, files in os.walk(indir):
            if 'manifest.json' in files and root.endswith('.unpacked'):
                rel = os.path.relpath(root, indir)
                base_rel = rel[:-len('.unpacked')] if rel.endswith('.unpacked') else rel
                outfile = os.path.join(outroot, base_rel)
                pack_dir(root, outfile)
                n += 1
        print('\n完成：%d 个目录已打包到 %s' % (n, outroot))
    else:
        outdir = args.input
        if args.output:
            outfile = args.output
        else:
            parent = os.path.dirname(outdir)
            name = os.path.basename(outdir)
            if name.endswith('.unpacked'):
                name = name[:-len('.unpacked')]
            outfile = os.path.join(parent, name)
        pack_dir(outdir, outfile)
        print('\n完成：%s' % outfile)


if __name__ == '__main__':
    main()
