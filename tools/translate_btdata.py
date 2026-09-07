#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Translate mx_btdata.cam STRT (5 multiplayer battle type names) and inject fonts."""
import sys, os, struct
sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam, parse_strt, build_strt, pack_cam

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'
WS = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace'

# Translations
BT_TRANSLATIONS = {
    'default': '默认',
    'Brute Force': '蛮力',
    'No Towers': '禁用塔楼',
    'One of a Kind': '独一无二',
    'Wizard War': '巫师之战',
}

# Read original
cam_path = os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_btdata.cam')
cam = read_cam(cam_path)

print(f"Sections: {len(cam['sections'])}")
for sec in cam['sections']:
    print(f"  {sec['ext']}: {len(sec['files'])} files")

replacements = {}

for sec in cam['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        print(f"\nSTRT file: {f['name']} ({len(f['raw'])} bytes)")
        strings = parse_strt(f['raw'])
        print(f"  Parsed: {len(strings)} strings")
        
        new_strings = []
        for s in strings:
            en = s['text']
            cn = BT_TRANSLATIONS.get(en)
            if cn:
                print(f"  [{s['id']}] {en!r} -> {cn!r}")
                new_strings.append({'id': s['id'], 'text': cn})
            else:
                print(f"  [{s['id']}] {en!r} (kept)")
                new_strings.append({'id': s['id'], 'text': en})
        
        new_raw = build_strt(new_strings, force_utf16=True)
        replacements[('STRT', f['name'])] = new_raw
        print(f"  Built: {len(new_raw)} bytes")

# Pack
print(f"\nPacking mx_btdata.cam...")
new_data = pack_cam(cam, replacements)
out_path = os.path.join(WS, 'output', 'DataMX', 'mx_btdata.cam')
with open(out_path, 'wb') as f:
    f.write(new_data)
print(f"  -> {out_path} ({len(new_data)} bytes)")

# Verify
print(f"\nVerifying...")
verify_cam = read_cam(out_path)
for sec in verify_cam['sections']:
    if sec['ext'] == 'STRT':
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            print(f"  STRT {f['name']}: {len(strings)} strings")
            for s in strings:
                print(f"    [{s['id']}] {s['text']!r}")
    elif sec['ext'] == 'BTDT':
        print(f"  BTDT: {len(sec['files'])} files")

# Now inject fonts
print(f"\nInjecting fonts...")
from inject_fonts import extract_font_section_from_m1, inject_font_section

m1_cam_path = r'G:\Projects\Majesty1\Data\textdata.cam'
m1_fonts = extract_font_section_from_m1(m1_cam_path)
if m1_fonts:
    inject_font_section(out_path, m1_fonts, out_path)
    print("Done!")
