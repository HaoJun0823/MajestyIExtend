#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create initial rollback.json from miss.log analysis"""
import json
import os

rollback_path = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts\rollback.json"

rollback = {
    # Resources
    "Gold": "金币",
    # Difficulty
    "Difficulty:": "难度：",
    "Beginner": "初级",
    "Advanced": "高级",
    "Expert": "专家",
    # Buildings
    "Embassy": "使馆",
    "Mausoleum": "陵墓",
    "Sorcerers Abode": "巫师住所",
    "Magic Bazaar": "魔法集市",
    "Level 2": "二级",
    "Level 3": "三级",
    "level one": "一级",
    "level two": "二级",
    "Level one": "一级",
    "Palace": "宫殿",
    "Temple to": "神殿",
    "Royal Gardens": "皇家花园",
    "Guardhouse": "哨塔",
    # UI labels
    "HIGH SCORES": "高分榜",
    "Requires:": "要求：",
    # Mission/quest keywords
    "The Forsaken Land": "被遗忘之地",
    "Rescue the Prince": "营救王子",
    "The Bell, the Book, and the Candle": "钟、书与烛",
    # Monsters
    "Goblins": "哥布林",
    "Ratman": "鼠人",
    # Common terms
    "Your Highness": "陛下",
    "settlement": "定居点",
    "Elven": "精灵",
    "enclave": "聚居地",
    "alliance": "联盟",
    "trade": "贸易",
    "merchants": "商人",
    "skirmishes": "冲突",
    "defenses": "防御",
    "kingdom": "王国",
    "heroes": "英雄",
    "village": "村庄",
    "invasion": "入侵",
    "Scouts": "侦察兵",
    "frontier": "边境",
    "monsters": "怪物",
    "settlements": "定居点",
}

with open(rollback_path, "w", encoding="utf-8") as f:
    json.dump(rollback, f, ensure_ascii=False, indent=2)

print(f"Created rollback.json with {len(rollback)} entries")
print(f"File size: {os.path.getsize(rollback_path)} bytes")

# Verify
with open(rollback_path, "r", encoding="utf-8") as f:
    v = json.load(f)
print(f"Verify: {len(v)} entries loaded back OK")
