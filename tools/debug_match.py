#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""调试：检查未匹配的HPTX条目的实际文本与翻译表key的差异"""
import os
import sys
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt
from round4_translations import ROUND4_TRANSLATIONS

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

# 加载未翻译列表
with open(os.path.join(os.path.dirname(__file__), 'untranslated.json'), 'r', encoding='utf-8') as f:
    untranslated = json.load(f)

needed_keys = [(u['file'], u['id']) for u in untranslated]

# 从 CAM 中提取完整文本
cam_files = [
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext'),
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'), 'textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
]

actual_texts = {}
for cam_path, label in cam_files:
    cam_data = read_cam(cam_path)
    for sec in cam_data['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            for s in strings:
                key = (f['name'], s['id'])
                if key in needed_keys:
                    actual_texts[key] = s['text']

# 比较
print("=== Checking unmatched entries ===\n")
for u in untranslated:
    key = (u['file'], u['id'])
    actual = actual_texts.get(key, '')
    if not actual:
        continue
    
    # Skip pure numbers
    if actual.strip().isdigit():
        continue
    
    # Check if normalized version matches any round4 key
    norm = actual.replace('\x01', '')
    
    found = False
    for r4_key in ROUND4_TRANSLATIONS:
        r4_norm = r4_key.replace('\x01', '')
        if norm == r4_norm:
            found = True
            break
    
    if not found:
        # Show first 100 chars of actual text with repr to see control chars
        print(f"[{u['file']}#{u['id']}]")
        print(f"  actual (repr): {repr(actual[:120])}")
        print()
