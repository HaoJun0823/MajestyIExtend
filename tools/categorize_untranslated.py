#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
第二遍翻译：处理未翻译的条目
"""
import json
import os

untrans_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\untranslated.json'
with open(untrans_path, 'r', encoding='utf-8') as f:
    items = json.load(f)

# 分类统计
categories = {}
for item in items:
    fname = item['file']
    if fname.startswith('GDB'):
        cat = 'Debugger'
    elif fname.startswith('HN59'):
        cat = 'Hero Titles'
    elif fname.startswith('HN58'):
        cat = 'Hero Names (Expansion)'
    elif fname.startswith('FNTX') or fname.startswith('FDTX'):
        cat = 'Map Names (Expansion)'
    elif fname.startswith('QUES'):
        cat = 'Quest Names (Expansion)'
    elif fname.startswith('QITM'):
        cat = 'Quest Items'
    elif fname.startswith('QEND'):
        cat = 'Quest Endings'
    elif fname.startswith('AP'):
        cat = 'UI Strings'
    elif fname == 'HKTX':
        cat = 'Hotkeys'
    elif fname == 'UNTN':
        cat = 'Unit Names'
    elif fname == 'MNTH':
        cat = 'Months'
    elif fname == 'GMTX':
        cat = 'Game Messages'
    else:
        cat = 'Other'
    if cat not in categories:
        categories[cat] = []
    categories[cat].append(item)

for cat, cat_items in sorted(categories.items(), key=lambda x: -len(x[1])):
    print(f"\n{cat} ({len(cat_items)}):")
    for it in cat_items[:15]:
        print(f"  [{it['file']}#{it['id']}] {it['text']}")
    if len(cat_items) > 15:
        print(f"  ... +{len(cat_items)-15} more")

# 保存分类结果
out_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\untranslated_categorized.json'
with open(out_path, 'w', encoding='utf-8') as f:
    json.dump(categories, f, ensure_ascii=False, indent=2)
