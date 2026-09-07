#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翻译质量抽检:
1. 从已部署 CAM 提取全部已翻译条目(en->cn 配对)
2. 检测明显问题: 中文里残留英文、机翻痕迹、术语不一致等
3. 抽样输出
"""
import os
import sys
import json
import re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

CAM_PAIRS = [
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'),    os.path.join(HD_GAME, 'backup_original', 'Data', 'textdata.cam'),    'textdata'),
    (os.path.join(HD_GAME, 'Data', 'gpltext.cam'),     os.path.join(HD_GAME, 'backup_original', 'Data', 'gpltext.cam'),     'gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'),  os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_gpltext.cam'),  'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'),  os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_rgstext.cam'),  'mx_rgstext'),
]

def is_cjk(s):
    return any(0x4E00 <= ord(c) <= 0x9FFF or 0x3400 <= ord(c) <= 0x4DBF for c in s)

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

pairs = []  # (file, id, en, cn, source)

for dep_path, orig_path, label in CAM_PAIRS:
    orig = extract_all(orig_path)
    dep = extract_all(dep_path)
    for fname in sorted(set(orig) | set(dep)):
        orig_d = orig.get(fname, {})
        dep_d = dep.get(fname, {})
        for sid in orig_d:
            en = orig_d[sid]
            cn = dep_d.get(sid, '')
            if not en or not en.strip():
                continue
            if is_cjk(cn):
                pairs.append((fname, sid, en, cn, label))

print(f"已翻译配对总数: {len(pairs)}")

issues = {
    'residual_english': [],   # 中文里残留长英文单词（未翻译完的痕迹）
    'mixed_width': [],        # 中英混杂
    'placeholder_break': [],  # 占位符丢失/错位
    'garbled': [],            # 乱码
}

# 1. 中文里残留英文单词（>=2个连续字母，排除格式符内的）
for fname, sid, en, cn, label in pairs:
    # 查找中文串中的英文残留
    for m in re.finditer(r'[A-Za-z]{2,}(?: [A-Za-z]{2,})*', cn):
        word = m.group()
        # 排除格式占位符
        if re.fullmatch(r'[%dDs]{1,4}|CD|CD-ROM|PC|TCP|IP|XP|DVD|GB|USB|OK|NO', word):
            continue
        issues['residual_english'].append((fname, sid, en, cn, word, label))

# 2. 中英混杂比例过高（中文句子含多个英文词）
for fname, sid, en, cn, label in pairs:
    cjk_chars = sum(1 for c in cn if 0x4E00 <= ord(c) <= 0x9FFF)
    en_words = re.findall(r'[A-Za-z]{2,}', cn)
    if cjk_chars > 0 and len(en_words) >= 2:
        # 过滤格式符
        real_words = [w for w in en_words if not re.fullmatch(r'[dDsSfF]{1,2}|OK|CD|PC', w)]
        if len(real_words) >= 2:
            issues['mixed_width'].append((fname, sid, en, cn, real_words, label))

# 3. 检查占位符完整性
for fname, sid, en, cn, label in pairs:
    en_ph = set(re.findall(r'%\w|%d|%s|%f|{.*?}', en))
    cn_ph = set(re.findall(r'%\w|%d|%s|%f|{.*?}', cn))
    # 允许中文新增全角括号等，只检查英文格式符是否丢失
    missing = en_ph - cn_ph
    if missing:
        # 排除那些本来就可能不是格式符的
        real_missing = [p for p in missing if p in ('%d', '%s', '%f')]
        if real_missing:
            issues['placeholder_break'].append((fname, sid, en, cn, list(missing), label))

# 输出
print(f"\n=== 中文残留英文单词 ===")
for fname, sid, en, cn, word, label in issues['residual_english'][:40]:
    print(f"  [{fname}#{sid}] 残留「{word}」: {cn[:80]}")
print(f"  ... 共 {len(issues['residual_english'])} 条")

print(f"\n=== 中英混杂 ===")
for fname, sid, en, cn, words, label in issues['mixed_width'][:30]:
    print(f"  [{fname}#{sid}] {cn[:80]}  <- {en[:60]}")
print(f"  ... 共 {len(issues['mixed_width'])} 条")

print(f"\n=== 占位符丢失 ===")
for fname, sid, en, cn, missing, label in issues['placeholder_break'][:20]:
    print(f"  [{fname}#{sid}] 缺 {missing}: {cn[:80]}  <- {en[:80]}")
print(f"  ... 共 {len(issues['placeholder_break'])} 条")

# 保存抽样
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scan_quality_issues.json')
with open(out, 'w', encoding='utf-8') as f:
    json.dump(issues, f, ensure_ascii=False, indent=2)
print(f"\n质量检查结果: {out}")