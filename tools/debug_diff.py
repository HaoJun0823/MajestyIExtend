#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""调试：逐字符比较未匹配条目"""
import os
import sys
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt
from round4_translations import ROUND4_TRANSLATIONS

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

# 提取 HPTX#943010152 的完整实际文本
cam_data = read_cam(os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'))
for sec in cam_data['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] != 'HPTX':
            continue
        strings = parse_strt(f['raw'])
        for s in strings:
            if s['id'] == 943010152:
                actual = s['text']
                print("=== ACTUAL TEXT ===")
                print(repr(actual[:200]))
                print()
                
                # Find closest round4 key
                for r4_key in ROUND4_TRANSLATIONS:
                    r4_norm = r4_key.replace('\x01', '')
                    actual_norm = actual.replace('\x01', '')
                    if actual_norm[:50] == r4_norm[:50]:
                        print("=== MATCHING ROUND4 KEY (first 200 chars) ===")
                        print(repr(r4_key[:200]))
                        print()
                        
                        # Find first difference
                        for i, (a, b) in enumerate(zip(actual, r4_key)):
                            if a != b:
                                print(f"First diff at position {i}:")
                                print(f"  actual[{i}] = {repr(a)} (ord={ord(a) if a else 'N/A'})")
                                print(f"  round4[{i}] = {repr(b)} (ord={ord(b) if b else 'N/A'})")
                                print(f"  Context actual: {repr(actual[max(0,i-10):i+10])}")
                                print(f"  Context round4: {repr(r4_key[max(0,i-10):i+10])}")
                                break
                        else:
                            print("No char diff found in overlapping range")
                            if len(actual) != len(r4_key):
                                print(f"Length diff: actual={len(actual)} vs round4={len(r4_key)}")
                        
                        # Also check normalized
                        actual_norm = actual.replace('\x01', '')
                        r4_norm2 = r4_key.replace('\x01', '')
                        print(f"\nNormalized comparison:")
                        print(f"  actual_norm len={len(actual_norm)}")
                        print(f"  round4_norm len={len(r4_norm2)}")
                        if actual_norm == r4_norm2:
                            print("  MATCH after normalization!")
                        else:
                            for i, (a, b) in enumerate(zip(actual_norm, r4_norm2)):
                                if a != b:
                                    print(f"  First norm diff at pos {i}: actual={repr(a)} vs round4={repr(b)}")
                                    print(f"  Context actual: {repr(actual_norm[max(0,i-10):i+10])}")
                                    print(f"  Context round4: {repr(r4_norm2[max(0,i-10):i+10])}")
                                    break
                        break
