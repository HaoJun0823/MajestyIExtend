#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查实际 CAM 文本中的换行模式"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt

HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

cam_data = read_cam(os.path.join(HD_GAME, 'DataMX', 'mx_gpltext.cam'))
for sec in cam_data['sections']:
    if sec['ext'] != 'STRT':
        continue
    for f in sec['files']:
        if f['name'] != 'HPTX':
            continue
        strings = parse_strt(f['raw'])
        for s in strings:
            if '\n\n\n' in s['text']:
                idx = s['text'].find('\n\n\n')
                print(f'ID={s["id"]}: {repr(s["text"][:30])}')
                print(f'  Context: {repr(s["text"][max(0,idx-5):idx+15])}')
                print()
