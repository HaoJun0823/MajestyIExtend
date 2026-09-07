#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
M1 HD 汉化 - 大规模漏译扫描 + 质量初筛

对比 backup_original(HD 原版英文) 与 游戏目录(已部署中文) 的 CAM STRT 文本:
1. 找出所有仍是英文(未被翻译)的条目
2. 分类统计
3. 对已翻译条目抽样输出用于质量检查

注意: 游戏目录的 CAM 是"翻译后"的, 文本是 UTF-16LE 中文。
backup_original 是原始英文。
"""
import os
import sys
import json
import re
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

# (部署路径, 原版路径, 标签)
CAM_PAIRS = [
    (os.path.join(HD_GAME, 'Data', 'textdata.cam'),    os.path.join(HD_GAME, 'backup_original', 'Data', 'textdata.cam'),    'textdata'),
    (os.path.join(HD_GAME, 'Data', 'gpltext.cam'),     os.path.join(HD_GAME, 'backup_original', 'Data', 'gpltext.cam'),     'gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_textdata.cam'), os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_textdata.cam'), 'mx_textdata'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'),  os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_gpltext.cam'),  'mx_gpltext'),
    (os.path.join(HD_GAME, 'DataMX', 'mx_rgstext.cam'),  os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_rgstext.cam'),  'mx_rgstext'),
]


def is_cjk(s):
    """是否包含 CJK 字符（视为已翻译）"""
    return any(0x4E00 <= ord(c) <= 0x9FFF or 0x3400 <= ord(c) <= 0x4DBF for c in s)


def extract_all(cam_path):
    """提取 CAM 中所有 STRT 条目: {name: {id: text}}"""
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


def main():
    untranslated = []   # 仍是英文的
    translated = []     # 已翻译的（用于抽检）
    total_files = {}

    for dep_path, orig_path, label in CAM_PAIRS:
        print(f"\n=== {label} ===")
        orig = extract_all(orig_path)
        dep = extract_all(dep_path)

        s_total = s_untrans = s_trans = 0
        for fname in sorted(set(orig) | set(dep)):
            orig_d = orig.get(fname, {})
            dep_d = dep.get(fname, {})
            for sid in orig_d:
                en = orig_d[sid]
                cn = dep_d.get(sid, '')
                s_total += 1
                # 过滤: 纯空白/纯数字/占位符字符串不统计
                if not en or not en.strip():
                    continue
                if is_cjk(cn):
                    s_trans += 1
                else:
                    # 未翻译: cn 里没有中文
                    s_untrans += 1
                    untranslated.append({
                        'file': fname, 'id': sid,
                        'en': en[:200], 'deployed': cn[:200],
                        'source': label
                    })
        total_files[label] = {'total': s_total, 'translated': s_trans, 'untranslated': s_untrans}
        print(f"  total={s_total} translated={s_trans} untranslated={s_untrans}")

    # 汇总
    print("\n" + "=" * 60)
    print("汇总")
    print("=" * 60)
    tot_t = tot_u = 0
    for k, v in total_files.items():
        print(f"  {k}: total={v['total']} translated={v['translated']} untranslated={v['untranslated']}")
        tot_t += v['translated']
        tot_u += v['untranslated']
    print(f"  TOTAL: translated={tot_t} untranslated={tot_u}")

    # 保存未翻译清单
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scan_untranslated.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(untranslated, f, ensure_ascii=False, indent=2)
    print(f"\n未翻译清单: {out_path} ({len(untranslated)} 条)")

    # 未翻译分类
    cats = {}
    for u in untranslated:
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

    print("\n未翻译分类:")
    for c, items in sorted(cats.items(), key=lambda x: -len(x[1])):
        print(f"  {c}: {len(items)}")
        for it in items[:8]:
            print(f"    [{it['file']}#{it['id']}] {it['en'][:80]}")
        if len(items) > 8:
            print(f"    ... +{len(items)-8} more")

    cat_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scan_untranslated_categorized.json')
    with open(cat_path, 'w', encoding='utf-8') as f:
        json.dump(cats, f, ensure_ascii=False, indent=2)
    print(f"分类保存: {cat_path}")


if __name__ == '__main__':
    main()