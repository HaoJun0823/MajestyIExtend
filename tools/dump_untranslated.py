#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import json

with open('untranslated.json', 'r', encoding='utf-8') as f:
    items = json.load(f)

# 导出全部未翻译条目
with open('untranslated_full.txt', 'w', encoding='utf-8') as f:
    for item in items:
        f.write(f'[{item["file"]}#{item["id"]}] {item["text"]}\n')

print(f'Exported {len(items)} items to untranslated_full.txt')

# HN** 条目（英雄名字）
hn_items = [i for i in items if i['file'].startswith('HN')]
print(f'\n=== HN** entries: {len(hn_items)} ===')
for item in hn_items:
    print(f'  [{item["file"]}#{item["id"]}] {item["text"]}')
