#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import json
import os
import sys

sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam, parse_strt

# 加载第三轮翻译
r3_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\round3_translations.json'
with open(r3_path, 'r', encoding='utf-8') as f:
    r3 = json.load(f)
ROUND3_EXACT = r3.get('exact', {})

# 加载补充翻译
sup_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\supplement_translations.json'
with open(sup_path, 'r', encoding='utf-8') as f:
    SUPPLEMENT = json.load(f)

# 检查QITM的实际文本
cam_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\DataMX\mx_gpltext.cam'
cam = read_cam(cam_path)

print("=== QITM actual text vs translation keys ===")
for sec in cam['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] == 'QITM':
            strings = parse_strt(f['raw'])
            for s in strings:
                text = s['text']
                # 查找匹配
                matched = text in ROUND3_EXACT or text in SUPPLEMENT
                if not matched:
                    # 检查哪个翻译表key最接近
                    best_match = None
                    best_score = 0
                    for key in list(ROUND3_EXACT.keys()) + list(SUPPLEMENT.keys()):
                        if 'Speed Tonic' in key and 'Speed Tonic' in text:
                            best_match = key
                            break
                        if 'Fire Balm' in key and 'Fire Balm' in text:
                            best_match = key
                            break
                    print(f"  [{s['id']}]")
                    print(f"    actual:  {repr(text[:100])}")
                    if best_match:
                        print(f"    key:     {repr(best_match[:100])}")
                        print(f"    translated: {ROUND3_EXACT.get(best_match, SUPPLEMENT.get(best_match, '???'))[:80]}")
                    else:
                        print(f"    no matching key found")
            break
    break
