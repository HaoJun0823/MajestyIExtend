#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import json

with open('untranslated.json', 'r', encoding='utf-8') as f:
    items = json.load(f)

# 非 HN 条目
non_hn = [i for i in items if not i['file'].startswith('HN')]
print(f'Non-HN entries: {len(non_hn)}')
for item in non_hn:
    text = item['text'][:120]
    print(f'  [{item["file"]}#{item["id"]}] {text}')
