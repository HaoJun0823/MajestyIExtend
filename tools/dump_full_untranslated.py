#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""导出未翻译条目的完整文本（非截断版）"""
import os
import sys
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

# 加载未翻译列表
with open(os.path.join(os.path.dirname(__file__), 'untranslated.json'), 'r', encoding='utf-8') as f:
    untranslated = json.load(f)

# 按 (file_name, id) 建索引
# 注意：id 在 JSON 中可能是 int
needed = {}  # (file_name, id) -> index_in_untranslated
for i, u in enumerate(untranslated):
    needed[(u['file'], u['id'])] = i

# 读取 5 个 CAM 文件
cam_files = [
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'), 'textdata'),
    (os.path.join(HD_GAME, 'Data', 'gpltext.cam'), 'gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext'),
]

results = [None] * len(untranslated)

for cam_path, label in cam_files:
    cam_data = read_cam(cam_path)
    
    for sec in cam_data['sections']:
        if sec['ext'] != 'STRT':
            continue
        
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            if not strings:
                continue
            
            for s in strings:
                key = (f['name'], s['id'])
                if key in needed:
                    idx = needed[key]
                    results[idx] = {
                        'file': f['name'],
                        'id': s['id'],
                        'text': s['text'],
                        'source_cam': label,
                    }

# 输出完整文本
out_path = os.path.join(os.path.dirname(__file__), 'untranslated_full.json')
with open(out_path, 'w', encoding='utf-8') as f:
    json.dump(results, f, ensure_ascii=False, indent=2)

print(f"Exported {sum(1 for r in results if r)} / {len(results)} entries")
print(f"Saved to: {out_path}")

# 也输出一个可读的文本格式
txt_path = os.path.join(os.path.dirname(__file__), 'untranslated_full.txt')
with open(txt_path, 'w', encoding='utf-8') as f:
    for i, r in enumerate(results):
        if r:
            f.write(f"### Entry {i+1} [{r['file']}#{r['id']}]\n")
            f.write(f"{r['text']}\n\n")

print(f"Also saved readable text to: {txt_path}")
