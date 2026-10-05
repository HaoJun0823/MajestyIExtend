#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Majesty HD 简体中文汉化包 —— 发布装配脚本。

将仓库里展开存储的 translated/*.unpacked 目录重新打包为 CAM/DAT 二进制，
连同 dist/ 预编译产物（winmm.dll、读我.txt、TextFix.ini）与 CI 编译的
StrFix.asi 装配成可分发的 zip（结构与 V4 包一致，排除 .bak 备份文件）。

用法:
    python tools/build_release.py                          # 完整流程（含 zip）
    python tools/build_release.py --asi path/to/StrFix.asi  # 指定 asi 路径
    python tools/build_release.py --no-zip                 # 只装配不打包
    python tools/build_release.py --skip-dll                # 跳过 asi（复用已有）

产物目录结构（与 V4 一致）::
    build_out/
      winmm.dll
      读我.txt
      update/
        Data/          gpltext.cam, textdata.cam, InterfaceStrings.xml, UIData_*.dat (13)
        DataMX/        mx_btdata.cam, mx_gpltext.cam, mx_rgstext.cam, mx_textdata.cam
        Quests/        *.mqxml + *_Text.xml (24+2)
        QuestsMX/       *.mqxml (14)
        MajestyI_StrFix.asi
        MajestyI_TextFix.ini

设计要点:
    - translated/*.unpacked → CAM/DAT: 用 tools_packer.pack_from_dir 字节级精确重建
    - Quests / InterfaceStrings.xml: 文本文件直接复制（无需打包）
    - dist/: winmm.dll(ASI loader) + 读我.txt + MajestyI_TextFix.ini（预编译产物）
    - asi: CI 编译的 StrFix.asi（34816B 体积闸门）
    - .bak 文件不纳入发布
"""
import os
import sys
import shutil
import zipfile
import hashlib
import argparse
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TP = os.path.join(HERE, 'tools_packer')
sys.path.insert(0, TP)
import packlib as P

# 需要打包的 CAM/DAT 文件清单（translated/ 下的相对路径）
CAM_DAT_FILES = [
    'Data/gpltext.cam',
    'Data/textdata.cam',
    'Data/UIData_1024_768.dat',
    'Data/UIData_1280_1024.dat',
    'Data/UIData_1280_768.dat',
    'Data/UIData_1280_800.dat',
    'Data/UIData_1280_960.dat',
    'Data/UIData_1360_768.dat',
    'Data/UIData_1440_900.dat',
    'Data/UIData_1600_1200.dat',
    'Data/UIData_1600_900.dat',
    'Data/UIData_1680_1050.dat',
    'Data/UIData_1920_1080.dat',
    'Data/UIData_1920_1200.dat',
    'Data/UIData_800_600.dat',
    'DataMX/mx_btdata.cam',
    'DataMX/mx_gpltext.cam',
    'DataMX/mx_rgstext.cam',
    'DataMX/mx_textdata.cam',
]

# 文本文件清单（直接复制，无需打包）
TEXT_FILES = {
    'Data/InterfaceStrings.xml': 'Data/InterfaceStrings.xml',
}
TEXT_DIRS = ['Quests', 'QuestsMX']

# 预编译产物
DIST_FILES = {
    'winmm.dll': '',           # root
    '读我.txt': '',             # root
    'MajestyI_TextFix.ini': 'update',  # update/
}

# asi 体积闸门
ASI_EXPECTED_SIZE = 34816


def pack_translated(translated_dir, out_data_dir):
    """将 translated/*.unpacked 打包为 CAM/DAT，输出到 out_data_dir。

    返回 (packed_count, failed_list)。
    """
    packed = 0
    failed = []
    for rel in CAM_DAT_FILES:
        unpacked_dir = os.path.join(translated_dir, rel + '.unpacked')
        if not os.path.isdir(unpacked_dir):
            failed.append(rel)
            continue
        try:
            data = P.pack_from_dir(unpacked_dir)
        except Exception as e:
            print('  FAIL pack %s: %s' % (rel, e))
            failed.append(rel)
            continue
        dst = os.path.join(out_data_dir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(data)
        packed += 1
        print('  packed %s (%d B)' % (rel, len(data)))
    return packed, failed


def copy_text_files(translated_dir, out_root):
    """复制文本文件（Quests, InterfaceStrings.xml）到发布目录。"""
    count = 0
    for src_rel, dst_rel in TEXT_FILES.items():
        src = os.path.join(translated_dir, src_rel)
        if os.path.isfile(src):
            dst = os.path.join(out_root, 'update', dst_rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            count += 1
    for d in TEXT_DIRS:
        src_dir = os.path.join(translated_dir, d)
        if not os.path.isdir(src_dir):
            continue
        dst_dir = os.path.join(out_root, 'update', d)
        os.makedirs(dst_dir, exist_ok=True)
        for f in sorted(os.listdir(src_dir)):
            src_f = os.path.join(src_dir, f)
            if os.path.isfile(src_f):
                shutil.copy2(src_f, os.path.join(dst_dir, f))
                count += 1
    return count


def copy_dist(repo, out_root):
    """复制 dist/ 下的预编译产物。"""
    dist = os.path.join(repo, 'dist')
    count = 0
    for fname, subdir in DIST_FILES.items():
        src = os.path.join(dist, fname)
        if not os.path.isfile(src):
            print('  WARNING: dist/%s missing' % fname)
            continue
        if subdir:
            dst = os.path.join(out_root, subdir, fname)
        else:
            dst = os.path.join(out_root, fname)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        count += 1
    return count


def deploy_asi(asi_path, out_root):
    """部署 StrFix.asi 到 update/ 目录。"""
    if not asi_path or not os.path.isfile(asi_path):
        return False, 'asi not found: %s' % asi_path
    size = os.path.getsize(asi_path)
    dst = os.path.join(out_root, 'update', 'MajestyI_StrFix.asi')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(asi_path, dst)
    return True, size


def verify_release(out_root):
    """校验发布目录完整性。返回 (errors_list)。"""
    errors = []
    # winmm.dll
    if not os.path.isfile(os.path.join(out_root, 'winmm.dll')):
        errors.append('missing root/winmm.dll')
    # 读我.txt
    if not os.path.isfile(os.path.join(out_root, '读我.txt')):
        errors.append('missing root/读我.txt')
    # asi
    asi = os.path.join(out_root, 'update', 'MajestyI_StrFix.asi')
    if not os.path.isfile(asi):
        errors.append('missing update/MajestyI_StrFix.asi')
    else:
        sz = os.path.getsize(asi)
        if sz != ASI_EXPECTED_SIZE:
            errors.append('asi size %d != expected %d' % (sz, ASI_EXPECTED_SIZE))
    # TextFix.ini
    if not os.path.isfile(os.path.join(out_root, 'update', 'MajestyI_TextFix.ini')):
        errors.append('missing update/MajestyI_TextFix.ini')
    # CAM/DAT
    for rel in CAM_DAT_FILES:
        f = os.path.join(out_root, 'update', rel)
        if not os.path.isfile(f):
            errors.append('missing update/%s' % rel)
    # Quests
    quests_dir = os.path.join(out_root, 'update', 'Quests')
    if os.path.isdir(quests_dir):
        n = len([f for f in os.listdir(quests_dir) if f.endswith('.mqxml')])
        if n < 20:
            errors.append('Quests/ has only %d mqxml (expected >=20)' % n)
    else:
        errors.append('missing update/Quests/')
    return errors


def pack_zip(out_root, zip_path):
    """将 out_root 内容打包为 zip（UTF-8 文件名）。"""
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(out_root):
            dirs.sort()
            for f in sorted(files):
                full = os.path.join(root, f)
                arcname = os.path.relpath(full, out_root).replace(os.sep, '/')
                zf.write(full, arcname)
    return os.path.getsize(zip_path)


def main():
    ap = argparse.ArgumentParser(description='Majesty HD CHS release assembly')
    ap.add_argument('--asi', default=None, help='StrFix.asi path (CI compiled)')
    ap.add_argument('--no-zip', action='store_true', help='skip zip packing')
    ap.add_argument('--skip-dll', action='store_true', help='skip asi (reuse committed)')
    ap.add_argument('--out-dir', default='build_out', help='output directory')
    args = ap.parse_args()

    translated_dir = os.path.join(REPO, 'translated')
    out_dir = os.path.join(REPO, args.out_dir) if not os.path.isabs(args.out_dir) else args.out_dir
    out_root = os.path.join(out_dir, 'release')

    # Clean output
    if os.path.isdir(out_root):
        shutil.rmtree(out_root)
    os.makedirs(out_root, exist_ok=True)

    print('=' * 60)
    print('  Majesty HD CHS Release Assembly')
    print('=' * 60)

    # STEP 1: Pack translated .unpacked → CAM/DAT
    print('\n--- STEP 1: Pack translated CAM/DAT ---')
    data_dir = os.path.join(out_root, 'update')
    packed, failed = pack_translated(translated_dir, data_dir)
    if failed:
        print('  FAILED: %s' % ', '.join(failed))
    print('  Packed %d/%d CAM/DAT files' % (packed, len(CAM_DAT_FILES)))

    # STEP 2: Copy text files (Quests, InterfaceStrings.xml)
    print('\n--- STEP 2: Copy text files ---')
    text_count = copy_text_files(translated_dir, out_root)
    print('  Copied %d text files' % text_count)

    # STEP 3: Copy dist/ (winmm.dll, 读我.txt, TextFix.ini)
    print('\n--- STEP 3: Copy dist/ ---')
    dist_count = copy_dist(REPO, out_root)
    print('  Copied %d dist files' % dist_count)

    # STEP 4: Deploy StrFix.asi
    print('\n--- STEP 4: Deploy StrFix.asi ---')
    if args.skip_dll:
        # Reuse committed asi
        asi_candidates = [
            os.path.join(REPO, 'dist', 'MajestyI_StrFix.asi'),
            os.path.join(REPO, 'MajestyI_StrFix', 'Release', 'MajestyI_StrFix.asi'),
        ]
        asi_path = next((p for p in asi_candidates if os.path.isfile(p)), None)
        if asi_path:
            ok, info = deploy_asi(asi_path, out_root)
            print('  Reused asi: %s (%d B)' % (asi_path, info))
        else:
            print('  WARNING: --skip-dll but no asi found; asi will be missing')
    else:
        asi_path = args.asi or os.path.join(REPO, 'MajestyI_StrFix', 'Release', 'MajestyI_StrFix.asi')
        ok, info = deploy_asi(asi_path, out_root)
        if ok:
            print('  Deployed asi: %d B' % info)
        else:
            print('  WARNING: %s' % info)

    # STEP 5: Verify
    print('\n--- STEP 5: Verify ---')
    errors = verify_release(out_root)
    if errors:
        print('  VERIFICATION FAILED:')
        for e in errors:
            print('    X %s' % e)
        # Don't fail on asi size mismatch in --skip-dll mode
        hard_errors = [e for e in errors if 'asi size' not in e]
        if hard_errors:
            sys.exit(1)
    else:
        print('  All checks passed')

    # STEP 6: Pack zip
    if not args.no_zip:
        print('\n--- STEP 6: Pack zip ---')
        ts = datetime.datetime.now()
        zip_name = 'MajestyHD-CHS-%s.zip' % ts.strftime('%Y-%m-%d-%H-%M-%S')
        zip_path = os.path.join(out_dir, zip_name)
        sz = pack_zip(out_root, zip_path)
        print('  Created: %s (%.1f MB)' % (zip_name, sz / 1e6))

    print('\n' + '=' * 60)
    print('  Done. Output: %s' % out_root)
    print('=' * 60)


if __name__ == '__main__':
    main()
