#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
检查 M1 原版中文术语(作为参考): 王宫/神殿/神庙 在 M1 原版中的用法
"""
import os
import sys
import re
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

ORIG_GAME = r'G:\Projects\Majesty1'

CAM_PATHS = [
    (os.path.join(ORIG_GAME, 'Data', 'textdata.cam'), 'textdata'),
    (os.path.join(ORIG_GAME, 'Data', 'gpltext.cam'), 'gpltext'),
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

# 统计 M1 原版中 王宫/宫殿/神庙/神殿 用法
counters = Counter()
samples = {'王宫': [], '宫殿': [], '神庙': [], '神殿': [], '寺庙': []}
for cam_path, label in CAM_PATHS:
    data = extract_all(cam_path)
    for fname, d in data.items():
        for sid, text in d.items():
            if not any(0x4E00 <= ord(c) <= 0x9FFF for c in text):
                continue
            for w in ['王宫', '宫殿', '神庙', '神殿', '寺庙']:
                if w in text:
                    counters[w] += 1
                    if len(samples[w]) < 5:
                        samples[w].append((label, fname, sid, text[:60]))

print("=== M1 原版中文术语统计 ===")
for w, cnt in counters.most_common():
    print(f"  {w}: {cnt}")
print()
for w, lst in samples.items():
    print(f"--- {w} 示例 ---")
    for l, f, s, t in lst:
        print(f"  [{l}/{f}#{s}] {t}")
    print()