#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最终术语残留统计: 精确统计各非标准术语的出现位置与数量
"""
import os
import sys
import json
import re
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

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

# 检查术语：裸词出现（不包含在已修正词中）
CHECKS = {
    '巫师(裸词, 非巫师住所等组合)': r'巫师',
    '道尔罗斯(应道罗斯)': r'道尔罗斯',
    '魔术师(应法师)': r'魔术师',
    '魔法师(应法师)': r'魔法师',
    '神庙(应神殿)': r'神庙',
    '寺庙(应神殿)': r'寺庙',
    '圣武士(应圣骑士)': r'圣武士',
    '流浪者(应游侠)': r'流浪者',
    '王宫(应宫殿?)': r'王宫',
    '神殿(正确用词,统计)': r'神殿',
    '宫殿(正确用词,统计)': r'宫殿',
}

# 采集所有中文文本
all_text = []
for cam_path, label in CAM_PATHS:
    data = extract_all(cam_path)
    for fname, d in data.items():
        for sid, text in d.items():
            if any(0x4E00 <= ord(c) <= 0x9FFF for c in text):
                all_text.append((label, fname, sid, text))

print(f"总中文条目: {len(all_text)}")
print()
for label, pattern in CHECKS.items():
    rx = re.compile(pattern)
    hits = [(l, f, s, t) for (l, f, s, t) in all_text if rx.search(t)]
    # 去重计数（同一文件同一 id 只算一次）
    print(f"【{label}】 {len(hits)} 条")
    for l, f, s, t in hits[:10]:
        print(f"  [{l}/{f}#{s}] {t[:80]}")
    if len(hits) > 10:
        print(f"  ... +{len(hits)-10}")
    print()