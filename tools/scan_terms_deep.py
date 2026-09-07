#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
深挖术语问题: 检查旧译名来源与替换覆盖
"""
import os
import sys
import json
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt
from glossary import TERM_REPLACE

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

print("=== 现有 TERM_REPLACE ===")
for k, v in TERM_REPLACE.items():
    print(f"  {k} -> {v}")

CAM_PATHS = [
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'), 'textdata'),
    (os.path.join(HD_GAME, 'Data', 'gpltext.cam'), 'gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext'),
]

def extract_all(cam_path):
    result = {}
    cam = read_cam(cam_path)
    for sec in cam['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            if not strings:
                continue
            d = result.setdefault(f['name'], {})
            for s in strings:
                d[s['id']] = s['text']
    return result

# 统计所有含"神庙"或"寺庙"的条目, 看上下文
print("\n=== 含『神庙/寺庙』的条目 ===")
count = 0
for cam_path, label in CAM_PATHS:
    data = extract_all(cam_path)
    for fname, d in data.items():
        for sid, text in d.items():
            if '神庙' in text or '寺庙' in text:
                print(f"  [{label}/{fname}#{sid}] {text[:100]}")
                count += 1
                if count > 30:
                    break
        if count > 30:
            break
    if count > 30:
        break

# 检查道尔罗斯/道罗斯
print("\n=== 道尔罗斯(应改道罗斯) ===")
count = 0
for cam_path, label in CAM_PATHS:
    data = extract_all(cam_path)
    for fname, d in data.items():
        for sid, text in d.items():
            if '道尔罗斯' in text:
                print(f"  [{fname}#{sid}] {text[:100]}")
                count += 1
print(f"  共 {count} 处")