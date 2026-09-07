#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Translate UIData_*.dat files - these contain main menu text like PLAY GAME, LOAD GAME.

UIData files are CYLBPC format with SMNU + STRT sections.
STRT is ASCII (flags=0x0200), needs to be rebuilt as UTF-16LE (flags=0x0208).
Also need to inject FONT section for CJK glyph rendering.
"""
import sys
import os
import struct

sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam, pack_cam, parse_strt, build_strt

# === UIData translation table ===
# Maps English text -> Chinese translation
# Using M2 terminology standard where applicable
UIDATA_TRANSLATIONS = {
    # AP29 - Main Menu
    'PLAY GAME': '开始游戏',
    'LOAD GAME': '加载游戏',
    'MULTIPLAYER': '多人游戏',
    'HIGH SCORES    ': '高分记录    ',
    'CINEMATIC': '动画演示',
    'CREDITS': '关于作者',
    'EXIT GAME': '退出游戏',
    'ADJUST SETTINGS': '调整设置',
    'Splash Screen': '开场画面',
    'Background Image': '背景图象',
    'Cheat Play Replay': '使用秘技重玩',
    'Version #': '版本号 ',
    'Gpl Debugger': 'Gpl 调试器',
    'CHANGE MODS': '更换模组',
    'ACTIVATE MODS': '激活模组',
    # Button triggers - internal names, keep as-is
    'play btn trigger 1': 'play btn trigger 1',
    'play btn trigger 2': 'play btn trigger 2',
    'play btn trigger 4': 'play btn trigger 4',
    'play btn trigger 3': 'play btn trigger 3',
    'credits btn trigger 1': 'credits btn trigger 1',
    'credits btn trigger 2': 'credits btn trigger 2',
    'credits btn trigger 3': 'credits btn trigger 3',
    'load btn trigger 1': 'load btn trigger 1',
    'score trigger 1': 'score trigger 1',
    'levels btn trigger 1': 'levels btn trigger 1',
    'cine btn trigger 1': 'cine btn trigger 1',
    'mplay btn trigger 1': 'mplay btn trigger 1',
    'exit btn trigger 1': 'exit btn trigger 1',

    # APd9/APda - Options Menu
    'APPLY': '应用',
    'Apply Changes': '应用更改',
    'BACK': '返回',
    'Return to Options Menu': '返回设置菜单',
    'SCREEN RESOLUTION': '屏幕分辨率',
    'Use VSync': '使用垂直同步',
    'Use DirectX 9': '使用 DirectX 9',
    'Windowed': '窗口模式',

    # APdc - Mods dialog
    'OK': '确定',
    'CANCEL': '取消',
    'Placeholder Revolving Quest Pog': '占位旋转任务标记',
    'Mods': '模组',
    'ADD': '添加',
    'REMOVE': '移除',
    'DESCRIPTION': '描述',
    'AVAILABLE': '可用',
    'ACTIVE': '已激活',
    'WORKS WITH': '兼容',

    # APdb - Quest selection menu
    'Bonus Quest 3': '奖励任务 3',
    'Bonus Quest 1': '奖励任务 1',
    'Bonus Quest 2': '奖励任务 2',
    'Spires of Death': '死亡之塔',
    'Urban Renewal': '城市更新',
    'Vigil': '守夜',
    'Rise of the Ratmen': '鼠人崛起',
    'Fortress of Ixmil': '伊克席尔要塞',
    'Legendary Heroes': '传奇英雄',
    'Clash of Empires': '帝国冲突',
    'Trade Routes': '贸易路线',
    'Scions of Chaos': '混沌之子',
    'Vale of Serpents': '蛇谷',
    'Darkness Falls': '黑暗降临',
    'The Siege': '围攻',
    'VIGIL cloud': '守夜云',
    'Spires Cloud': '死亡之塔云',
    'Load Downloadable Quest (hidden until available)': '加载可下载任务（可用前隐藏）',
    'Clash of Empires': '帝国冲突',
    'Fortress Ixmil': '伊克席尔要塞',
    'Siege': '围攻',
    'Trade  Routes': '贸易路线',
    'Vale Serpents': '蛇谷',
    'Elven Treachery': '精灵背叛',
    'Day of Reckoning': '审判之日',
    'The Holy Chalice': '圣杯',
    "THe Wizard's Curse": '巫师的诅咒',
    'Deal with the Demon': '与恶魔交易',
    'The Bell Book and Candle': '钟书烛',
    "Brashnard's Sphere": '布拉什纳德之球',
    'The Dark Forest': '黑暗森林',
    'The Liche Queen': '巫妖女王',
    'Tomb of the Dragon King': '龙王之墓',
    'Goblin Hordes': '哥布林大军',
    'Slay the Mighty Dragon': '屠龙',
    'Slave Pits': '奴隶坑',
    'The Magic Ring': '魔戒',
    'Save the Prince': '拯救王子',
    'The Forsaken Lands': '被遗弃之地',
    'The Fertile Plains': '丰饶平原',
    'The Quest for the Crown': '王冠之 quest',
    'Quest Name Background art': '任务名称背景图',
    'holy chalice': '圣杯',
    'tomb of dragon king': '龙王之墓',
    'day of reckoning': '审判之日',
    'elven treachery': '精灵背叛',
    'Bonus Downloadable Quest 1 Trigger': '奖励下载任务1触发器',
    'Bonus Downloadable Quest 3 Trigger': '奖励下载任务3触发器',
    'Bonus Downloadable Quest 2 Trigger': '奖励下载任务2触发器',
    'ERASE VICTORIES': '清除胜利记录',
    'MAIN MENU': '主菜单',
    'Freestyle Game': '自由模式游戏',
    'num    easy medium hard number of times played              high score etc         easy medium hardnumber of times played              high score etc         easy medium hardnumber of times played              high score etc         easy medium hard': '数量    简单 中等 困难 游玩次数              高分 等         简单 中等 困难游玩次数              高分 等         简单 中等 困难游玩次数              高分 等         简单 中等 困难',
    'quest notes': '任务笔记',
    'Current Quest Name': '当前任务名称',
    'FREESTYLE QUESTS': '自由模式任务',
    'Cheat All Quests': '秘技：全部任务',
}


def translate_uidata_file(input_path, output_path, m1_fonts):
    """Translate a single UIData file."""
    print(f"\n--- {os.path.basename(input_path)} ---")
    cam = read_cam(input_path)
    
    # Build replacements for STRT files
    replacements = {}
    translated_count = 0
    total_count = 0
    
    for sec in cam['sections']:
        if sec['ext'] == 'STRT':
            for f in sec['files']:
                strings = parse_strt(f['raw'])
                new_strings = []
                for s in strings:
                    total_count += 1
                    eng_text = s['text']
                    if eng_text in UIDATA_TRANSLATIONS:
                        new_text = UIDATA_TRANSLATIONS[eng_text]
                        translated_count += 1
                    else:
                        new_text = eng_text  # Keep as-is
                    new_strings.append({'id': s['id'], 'text': new_text})
                
                # Rebuild as UTF-16LE
                new_raw = build_strt(new_strings, force_utf16=True)
                replacements[(sec['ext'], f['name'])] = new_raw
    
    # Add FONT section
    has_font = any(sec['ext'] == 'FONT' for sec in cam['sections'])
    if has_font:
        cam['sections'] = [sec for sec in cam['sections'] if sec['ext'] != 'FONT']
    
    font_section = {
        'ext': 'FONT',
        'ext_raw': b'FONT',
        'offset': 0,
        'files': [],
    }
    for font in m1_fonts:
        font_section['files'].append({
            'name': font['name'],
            'offset': 0,
            'size': len(font['data']),
            'raw': font['data'],
        })
    cam['sections'].append(font_section)
    
    # Pack
    new_data = pack_cam(cam, replacements)
    
    # Write
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(new_data)
    
    print(f"  Translated: {translated_count}/{total_count} strings")
    print(f"  Output: {len(new_data):,} bytes")
    
    # Verify
    verify_cam = read_cam(output_path)
    print(f"  Verify sections: {len(verify_cam['sections'])}")
    for sec in verify_cam['sections']:
        print(f"    {sec['ext']}: {len(sec['files'])} files")
    
    return translated_count, total_count


def main():
    # Extract M1 fonts
    m1_cam_path = r'G:\Projects\Majesty1\Data\textdata.cam'
    print("Extracting M1 fonts...")
    m1_cam = read_cam(m1_cam_path)
    m1_fonts = []
    for sec in m1_cam['sections']:
        if sec['ext'] == 'FONT':
            for f in sec['files']:
                m1_fonts.append({'name': f['name'], 'data': f['raw']})
                print(f"  {f['name']}: {f['size']:,} bytes")
    
    # Process all UIData files
    data_dir = r'I:\SteamLibrary\steamapps\common\Majesty HD\backup_original\Data'
    output_dir = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\Data'
    
    uidata_files = sorted([f for f in os.listdir(data_dir) if f.startswith('UIData_')])
    print(f"\nProcessing {len(uidata_files)} UIData files...")
    
    total_translated = 0
    total_strings = 0
    for fname in uidata_files:
        input_path = os.path.join(data_dir, fname)
        output_path = os.path.join(output_dir, fname)
        t, s = translate_uidata_file(input_path, output_path, m1_fonts)
        total_translated += t
        total_strings += s
    
    print(f"\n=== Summary ===")
    print(f"  Total: {total_translated}/{total_strings} strings translated")
    print(f"  Files: {len(uidata_files)} UIData files processed")


if __name__ == '__main__':
    main()
