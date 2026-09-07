#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import json
import os
import sys

sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam, parse_strt

# 检查 QITM 实际文本
cam_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\DataMX\mx_gpltext.cam'
cam = read_cam(cam_path)

for sec in cam['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] == 'QITM':
            strings = parse_strt(f['raw'])
            print(f"QITM: {len(strings)} strings")
            for s in strings[:5]:
                text = s['text']
                print(f"  [{s['id']}] repr={repr(text[:100])}")
            break
    break

# 检查 HPTX
for sec in cam['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] == 'HPTX':
            strings = parse_strt(f['raw'])
            print(f"\nHPTX: {len(strings)} strings")
            for s in strings[:3]:
                text = s['text']
                print(f"  [{s['id']}] repr={repr(text[:120])}")
            break
    break

# 检查 FDTX
for sec in cam['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] == 'FDTX':
            strings = parse_strt(f['raw'])
            print(f"\nFDTX: {len(strings)} strings")
            for s in strings[:3]:
                text = s['text']
                print(f"  [{s['id']}] repr={repr(text[:120])}")
            break
    break
