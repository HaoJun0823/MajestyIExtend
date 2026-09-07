#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建独立的 FONT section CAM 文件

从 M1 原版 textdata.cam 中提取 FONT section 的 5 个 fnt 文件,
构建一个只含 FONT section 的独立 CAM 文件,
放入 update/Data/ 目录.

游戏加载时会将此 CAM 的 FONT section 合并到主 textdata.cam 中,
提供 CJK 位图字形容量.

CAM 文件格式:
  Header: CYLBPC(6) + padding(6) + num_sections(4) + dummy(4) = 20 bytes
  Section table: (ext(4) + offset(4)) * N
  Section data: num_files(4) + dummy(4) + (name(20) + offset(4) + size(4)) * N
  File data region: raw file data
"""
import struct
import sys
import os

sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam

# ============================================================
# 路径
# ============================================================
M1_CAM = r'G:\Projects\Majesty1\Data\textdata.cam'
OUTPUT_DIR = r'I:\SteamLibrary\steamapps\common\Majesty HD\update\Data'
OUTPUT_PATH = os.path.join(OUTPUT_DIR, 'fontdata.cam')

# ============================================================
# 1. 从 M1 CAM 提取 FONT section
# ============================================================
print("Reading M1 textdata.cam...")
m1_cam = read_cam(M1_CAM)

font_files = []
for sec in m1_cam['sections']:
    if sec['ext'] == 'FONT':
        for f in sec['files']:
            print(f"  {f['name']}: {f['size']:,} bytes")
            font_files.append({
                'name': f['name'],
                'data': f['raw'],
            })

if not font_files:
    print("ERROR: No FONT section found in M1 CAM!")
    sys.exit(1)

print(f"\nTotal fonts: {len(font_files)}, total size: {sum(len(f['data']) for f in font_files):,} bytes")

# ============================================================
# 2. 构建独立 CAM 文件
# ============================================================
print("\nBuilding standalone FONT CAM...")

num_sections = 1
num_files = len(font_files)

# Header: CYLBPC(6) + padding(6) + num_sections(4) + dummy(4) = 20 bytes
header = b'CYLBPC' + b'\x00' * 6 + struct.pack('<I', num_sections) + struct.pack('<I', 0)

# Section table: 1 entry = ext(4) + offset(4) = 8 bytes
# Section offset = 20 (header) + 8 (section table) = 28
section_offset = 20 + 8 * num_sections

section_table = b'FONT' + struct.pack('<I', section_offset)

# Section data: num_files(4) + dummy(4) + (name(20) + offset(4) + size(4)) * num_files
section_header = struct.pack('<I', num_files) + struct.pack('<I', 0)

# File entries
file_entries = bytearray()
# Data region starts after header + section_table + section_data
data_start = 20 + 8 * num_sections + 8 + 28 * num_files

current_offset = data_start
for f in font_files:
    name_bytes = f['name'].encode('ascii').ljust(20, b'\x00')[:20]
    file_entries.extend(name_bytes)
    file_entries.extend(struct.pack('<I', current_offset))
    file_entries.extend(struct.pack('<I', len(f['data'])))
    current_offset += len(f['data'])

# Data region
data_region = bytearray()
for f in font_files:
    data_region.extend(f['data'])

# Assemble
cam_data = bytearray()
cam_data.extend(header)
cam_data.extend(section_table)
cam_data.extend(section_header)
cam_data.extend(file_entries)
cam_data.extend(data_region)

# Verify layout
assert len(cam_data) == data_start + sum(len(f['data']) for f in font_files), \
    f"Layout mismatch: {len(cam_data)} != {data_start + sum(len(f['data']) for f in font_files)}"

# ============================================================
# 3. 写入输出文件
# ============================================================
os.makedirs(OUTPUT_DIR, exist_ok=True)
with open(OUTPUT_PATH, 'wb') as f:
    f.write(cam_data)

print(f"Written: {OUTPUT_PATH}")
print(f"Size: {len(cam_data):,} bytes")

# ============================================================
# 4. 验证: 读回并检查
# ============================================================
print("\nVerifying...")
verify_cam = read_cam(OUTPUT_PATH)
for sec in verify_cam['sections']:
    ext = sec['ext']
    nfiles = len(sec['files'])
    print(f"  Section {ext}: {nfiles} files")
    for f in sec['files']:
        print(f"    {f['name']}: {f['size']:,} bytes")

# 与 M1 原始数据对比
print("\nVerifying data integrity...")
verify_cam2 = read_cam(OUTPUT_PATH)
m1_cam2 = read_cam(M1_CAM)

m1_fonts = {}
for sec in m1_cam2['sections']:
    if sec['ext'] == 'FONT':
        for f in sec['files']:
            m1_fonts[f['name']] = f['raw']

all_match = True
for sec in verify_cam2['sections']:
    if sec['ext'] != 'FONT':
        continue
    for f in sec['files']:
        orig = m1_fonts.get(f['name'])
        if orig == f['raw']:
            print(f"  {f['name']}: OK (matches M1 original)")
        else:
            print(f"  {f['name']}: MISMATCH!")
            all_match = False

if all_match:
    print("\nAll font data verified OK!")
else:
    print("\nWARNING: Data mismatch detected!")

print("\nDone!")
