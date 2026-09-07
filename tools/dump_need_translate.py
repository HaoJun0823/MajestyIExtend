#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打印需翻译清单全文（去重后），便于审查"""
import json
import os

base = r"I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools"

with open(os.path.join(base, 'scan_need_translate.json'), 'r', encoding='utf-8') as f:
    need = json.load(f)

# 按 (file,id) 去重
seen = {}
for u in need:
    seen[(u['file'], u['id'])] = u
uniq = list(seen.values())
print(f"原始: {len(need)} -> 去重后: {len(uniq)}")

# 按文件分组输出
by_file = {}
for u in uniq:
    by_file.setdefault(u['file'], []).append(u)

for fn in sorted(by_file.keys()):
    items = by_file[fn]
    print(f"\n### {fn} ({len(items)} 条)")
    for it in items:
        en = it['en'].replace('\n', '\\n')
        print(f"  [{it['id']}] {en}")