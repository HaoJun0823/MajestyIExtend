#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
术语一致性检查:
扫描已部署译文，检查是否包含 M1 旧译名/非 M2 标准术语
"""
import os
import sys
import json
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

CAM_PATHS = [
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'), 'textdata'),
    (os.path.join(HD_GAME, 'Data', 'gpltext.cam'), 'gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext'),
]

# 疑似非 M2 标准术语（M1 旧译名/常见错译）
SUSPECT_TERMS = [
    '安格瑞拉', '弗尔瓦斯', '海尔利亚', '克罗尔姆', '克莱普塔', '鲁恩纳德',
    '寺庙', '神庙',           # 应为 神殿
    '流浪者',                # 应为 游侠
    '盗贼',                  # 应为 女盗贼
    '巫师',                  # 应为 法师
    '圣武士',                # 应为 圣骑士
    '警卫塔楼',              # 应为 守卫塔
    '矮人驻地',              # 应为 矮人公会
    '来访者',                # 应为 访客
    '信息',                  # 应为 消息
    '载入游戏',              # 应为 加载游戏
    '制作人员名单',          # 应为 关于作者
    '多人游戏',              # 应为 多人模式
    '主菜单',                # 应为 菜单
    '选项',                  # 应为 设置
    '魔术师',                # 应为 法师
    '魔法师',                # 应为 法师
    '盗贼公会',              # 应为 女盗贼公会
    '铁匠铺',                # M2 是 铁匠铺（一致）
    '哥布林',                # 一致
]

def extract_all(cam_path):
    result = {}
    cam = read_cam(cam_path)
    for sec in cam['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            if not strings:
                continue
            d = result.setdefault(f['name'], {})
            for s in strings:
                d[s['id']] = s['text']
    return result

hits = []
for cam_path, label in CAM_PATHS:
    data = extract_all(cam_path)
    for fname, d in data.items():
        for sid, text in d.items():
            # 仅检查含中文的条目
            if not any(0x4E00 <= ord(c) <= 0x9FFF for c in text):
                continue
            for term in SUSPECT_TERMS:
                if term in text:
                    hits.append((fname, sid, term, text, label))

# 去重统计
from collections import Counter
term_count = Counter(h[2] for h in hits)

print("=== 疑似术语不一致统计 ===")
for term, cnt in term_count.most_common():
    print(f"  「{term}」: {cnt} 处")

print("\n=== 具体位置（每条最多 10 例） ===")
seen_term = Counter()
for fname, sid, term, text, label in hits:
    if seen_term[term] >= 10:
        continue
    seen_term[term] += 1
    print(f"  [{label}/{fname}#{sid}] {term}: {text[:90]}")

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scan_term_issues.json')
with open(out, 'w', encoding='utf-8') as f:
    json.dump(hits, f, ensure_ascii=False, indent=2)
print(f"\n保存: {out} ({len(hits)} 条)")