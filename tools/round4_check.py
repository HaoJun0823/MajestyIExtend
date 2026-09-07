#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第四轮：处理最后205条未翻译条目
问题：untranslated.json中文本被截断到80字符，但实际CAM中文本更长
方案：用startswith前30字符模糊匹配
"""
import json
import os

# 加载未翻译条目
untrans_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\untranslated.json'
with open(untrans_path, 'r', encoding='utf-8') as f:
    items = json.load(f)

# 加载第三轮翻译
r3_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\round3_translations.json'
with open(r3_path, 'r', encoding='utf-8') as f:
    r3 = json.load(f)

ROUND3_EXACT = r3.get('exact', {})
ROUND3_TEMPLATES = [(t[0], t[1]) for t in r3.get('templates', [])]

# 检查哪些条目通过startswith已经能匹配
matched = 0
unmatched = []
for item in items:
    text = item['text']
    found = False
    # 精确
    if text in ROUND3_EXACT:
        found = True
    # 模板
    if not found:
        for prefix, cn in ROUND3_TEMPLATES:
            if text.startswith(prefix[:50]):
                found = True
                break
    if found:
        matched += 1
    else:
        unmatched.append(item)

print(f"Already matched: {matched}")
print(f"Still unmatched: {len(unmatched)}")

# 查看未匹配的条目
for item in unmatched[:20]:
    text = item['text'][:100]
    print(f"  [{item['file']}#{item['id']}] {text}")
print(f"  ... total {len(unmatched)}")
