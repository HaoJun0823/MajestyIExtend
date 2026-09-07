#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分析未翻译清单: 区分"真正需要翻译" vs "无需翻译(数字/单字母/键名/格式符等)"
"""
import json
import os
import re

base = r"I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools"

with open(os.path.join(base, 'scan_untranslated.json'), 'r', encoding='utf-8') as f:
    untranslated = json.load(f)

# 过滤规则（无需翻译）
def is_no_translate(text):
    t = text.strip()
    if not t:
        return True
    # 单字符（字母/符号/数字）
    if len(t) == 1:
        return True
    # 纯数字
    if re.fullmatch(r'\d[\d,\. ]*', t):
        return True
    # 纯格式符/占位符: %d %s {0} 等
    if re.fullmatch(r'[%\d\s\{\}\[\]\(\)\-\.:,，。·%#&+_=<>/\\|!?~`\'\"]+', t):
        return True
    # 键名
    if t in ('Alt', 'Ctrl', 'Shift', 'Tab', 'Return', 'ESC', 'Spc', 'Del', 'Backspace',
             'PageUp', 'PageDn', 'Home', 'End', 'Up', 'Dn', 'Left', 'Right', 'Insert',
             'Delete', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10',
             'F11', 'F12', 'Space', 'Enter', 'Esc', 'Bck', 'Nxt', 'Prv'):
        return True
    # 纯空白分隔符
    if re.fullmatch(r'[-\s|_=\*#\.]{2,}', t):
        return True
    return False

need = []      # 需要翻译
no_need = []   # 无需翻译
doubt = []     # 可疑（边界）

for u in untranslated:
    en = u['en']
    if is_no_translate(en):
        no_need.append(u)
    else:
        # 含常见可翻译单词，进一步筛选
        if re.search(r'[A-Za-z]{2,}', en):
            need.append(u)
        else:
            doubt.append(u)

print(f"未翻译总数: {len(untranslated)}")
print(f"  无需翻译(数字/键名/单字符/格式符): {len(no_need)}")
print(f"  需要翻译(含英文单词): {len(need)}")
print(f"  边界情况: {len(doubt)}")

# 需要翻译的按类别统计
cats = {}
for u in need:
    fn = u['file']
    if fn.startswith('GDB'):
        c = 'Debugger'
    elif fn.startswith('HN59'):
        c = '英雄称号'
    elif fn.startswith('HN58'):
        c = '英雄名(扩展)'
    elif fn.startswith('FNTX') or fn.startswith('FDTX'):
        c = '地图名(扩展)'
    elif fn.startswith('QUES'):
        c = '任务名(扩展)'
    elif fn.startswith('QITM'):
        c = '任务物品'
    elif fn.startswith('QEND'):
        c = '任务结局'
    elif fn.startswith('AP'):
        c = 'UI字符串'
    elif fn == 'HKTX':
        c = '热键'
    elif fn == 'UNTN':
        c = '单位名'
    elif fn == 'MNTH':
        c = '月份'
    elif fn == 'GMTX':
        c = '游戏消息'
    else:
        c = '其他'
    cats.setdefault(c, []).append(u)

print("\n需翻译分类:")
for c, items in sorted(cats.items(), key=lambda x: -len(x[1])):
    print(f"  {c}: {len(items)}")
    for it in items[:15]:
        print(f"    [{it['file']}#{it['id']}] {it['en'][:100]}")
    if len(items) > 15:
        print(f"    ... +{len(items)-15} more")

# 保存
out_need = os.path.join(base, 'scan_need_translate.json')
out_noneed = os.path.join(base, 'scan_no_need_translate.json')
with open(out_need, 'w', encoding='utf-8') as f:
    json.dump(need, f, ensure_ascii=False, indent=2)
with open(out_noneed, 'w', encoding='utf-8') as f:
    json.dump(no_need, f, ensure_ascii=False, indent=2)
print(f"\n需翻译清单: {out_need}")
print(f"无需翻译清单: {out_noneed}")