#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Clean up rollback.json:
1. Add long phrases (On Research:, On Buildings:, etc.)
2. Remove harmful short words that cause substring contamination
3. Keep name fragments that are safe (long enough or unique enough)
"""

import json, os

rb_path = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts\rollback.json"

with open(rb_path, "r", encoding="utf-8") as f:
    rb = json.load(f)

print(f"Original rollback.json: {len(rb)} entries")

# ============================================================
# 1. Add long phrases (these will be matched first after sorting)
# ============================================================
long_phrases = {
    # Finance report phrases
    " On Research:": " 研究支出:",
    " On Buildings:": " 建造支出:",
    " On Heroes:": " 英雄支出:",
    " On Spells:": " 魔法支出:",
    " On Flags:": " 旗帜支出:",
    " Total Made:": " 财产总额:",
    " Gold Balance:": " 金币余额:",
    " Gold Initial:": " 初始金币:",
    " Total Spent:": " 总花费:",
    " Constructed:": " 已建造:",
    " Destroyed:": " 已摧毁:",
    " Standing:": " 现存:",
    " Recruited:": " 已招募:",
    " Killed:": " 已阵亡:",
    " Alive:": " 存活:",
    " Average Level:": " 平均等级:",
    "Danger Level:": "危险等级:",
    " buried here": " 埋葬于此",
    "Vice:": "恶习:",
    # Building + gold (longer than just building name)
    " Wizards Tower": " 魔法塔",
    " Wizards Guild": " 魔法行会",
    " Warriors Guild": " 战士行会",
    " Rangers Guild": " 游侠行会",
    " Rogues Guild": " 盗贼行会",
    # Cast spell context phrases
    "Cast ": "施放",
    "Cast Re": "施用",  # Cast Resurrection, Cast Reanimate
    "Cast B": "施用",  # Cast Blessing
    "Cast H": "施展",  # Cast Healing
    "Cast S": "施用",  # Cast Super Charge, Stone Skin, Sun Scorch
    "Cast V": "施用",  # Cast Vines, Vigilance
    "Cast I": "施展",  # Cast Illusionary Hero, Invisibility
    "Cast A": "施用",  # Cast Animate Bones, Anti-magic Shield
    "Cast W": "施用",  # Cast Wither, Wind Storm, Winged Feet
    "Cast P": "施用",  # Cast Petrify
    "Cast F": "施用",  # Cast Fire Strike, Farseeing, Frost Field
    "Cast E": "施放",  # Cast Earthquake
    "Cast C": "施放",  # Cast Change of Heart, Chain Lightning
    "Cast G": "施放",  # Cast Gate
    "Cast D": "施放",  # Cast Dismiss
    "Cast L": "施放",  # Cast Lightning Bolt, Lightning Storm
    "- (Unavailable: requires ": "-（不可用：需要",
    "Unavailable: requires": "不可用：需要",
    "Sorcerers Abode": "巫师住所",
    "Temple to Agrela": "阿格雷拉神庙",
    "Temple to Dauros": "道尔罗斯神庙",
    "Temple to Fervus": "费尔弗斯神庙",
    "Temple to Helia": "赫莉娅神庙",
    "Temple to Krolm": "克罗尔玛神庙",
    "Temple to Krypta": "墓穴神庙",
    "Temple to Lunord": "卢诺德神庙",
    "Wizards Guild Level 3": "3级魔法行会",
    "Wizards Guild Level 2": "2级魔法行会",
    "Palace Guard": "王宫守卫",
    "Tax Collector": "收税员",
    "Ballista Tower": "弩炮楼",
    "Dwarven Settlement": "矮人公会",
    "Magic Bazaar": "魔法集市",
    "Royal Gardens": "皇家花园",
    "Hall Of Champions": "英杰殿",
    "Hall of Champions": "英杰殿",
    "Trading Post": "交易所",
    "Elven Bungalow": "精灵营房",
    "Gnome Hovel": "地精窝",
    "Fairgrounds": "竞技场",
    "Blacksmith Level 3": "铁匠铺 Level 3",
    "Blacksmith Level 2": "铁匠铺 Level 2",
    "Guardhouse Level 2": "升级守卫塔",
    "Library Level 2": "2级图书馆",
    "Marketplace Level 3": "3级市场",
    "Marketplace Level 2": "2级市场",
    "Rogues Guild Level 2": "2级盗贼行会",
    # Hero/unit names (keep as rollback but longer forms)
    "Palace Level 2": "2级王宫",
    "Palace Level 3": "3级王宫",
    # Common UI phrases
    " of 700 hp": " of 700 hp",
    " of 350 hp": " of 350 hp",
    " of 400 hp": " of 400 hp",
    " of 300 hp": " of 300 hp",
    " of 600 hp": " of 600 hp",
    " of 550 hp": " of 550 hp",
    " of 75 hp": " of 75 hp",
}

for k, v in long_phrases.items():
    if k not in rb:
        rb[k] = v
        # print(f"  Added: {k!r} -> {v!r}")

print(f"After adding long phrases: {len(rb)} entries")

# ============================================================
# 2. Remove harmful short words
# ============================================================
# These are the ones causing substring contamination in rollback.log
harmful_short = {
    # From rollback.log analysis:
    "Sea": "瑞恩伯",        # matched inside "Research" -> "Re瑞恩伯rch"
    "Building": "正在建造",  # matched inside "Buildings" -> "正在建造s"
    "Wizards": "法师",      # "Wizards Tower" -> "法师s Tower"
    "Wizards Tower": "魔法塔", # Will be handled by long phrase above (with space prefix)
    "Wizard": "法师",       # similar
    "Build": "建造",        # might match inside other words
    "Cast": "施放",         # too short, too many false matches (already have "Cast " with space)
    "Heal": "治疗",         # matches "Healing" -> "治疗ing"
    "Gate": "传送门",       # matches "Gateway" etc.
    "Vine": "藤",           # matches "Vines" -> "藤s"
    "Flame": "火焰",        # might match inside words
    "Stone": "石化",       # matches "Stones" -> "石化s"
    "Vigil": "守夜",        # matches "Vigilance" -> "守夜ance"
    "Vigor": "活力",        # matches "Vigorous" etc.
    "Slow": "减速",         # might match inside words
    "Rogue": "女盗贼",      # matches "Rogues" -> "女盗贼s"
    "Gnome": "地精",        # might match inside words
    "Dwarf": "矮人",        # matches "Dwarven" -> "矮人en"
    "Harpy": "鹰身女妖",   # matches "Harpies" -> "鹰身女妖ies"
    "Curse": "诅咒",        # matches "Cursed" -> "诅咒d"
    "Haste": "加速",        # might match inside words
    "Siege": "围攻",        # might match inside words
    "Dodge": "闪避",        # might match inside words
    "Parry": "格挡",        # might match inside words
    "Empty": "空的",        # matches "Emptied" etc.
    "Clear": "清除",        # might match inside words
    "Reset": "重置",        # might match inside words
    "Ready": "就绪",        # might match inside words
    "Adept": "信徒",        # might match inside words
    "Mace": "钉头锤",       # might match inside words
    "Bird": "鸟",           # might match inside words
    "Toad": "蟾",           # might match inside words
    "Moss": "苔",           # might match inside words
    "Mist": "薄雾",         # might match inside words
    "Smoke": "烟雾",        # might match inside words
    "Vines": "藤蔓术",      # matches "Vines" in other contexts
    "Solar": "太阳骑士",    # matches "Solarium" etc.
    "Snake": "西里",        # might match inside words
    "Lich": "巫妖",         # matches "Liche" etc.
    "Monk": "僧侣",         # might match inside words
    "Off": "关闭",          # matches "Office" etc.
    "Yes": "是",            # too short, too common
    "Tree": "格莱德",      # matches "Trees" -> "格莱德s"
    "House": "住房",        # matches "Household" etc. - actually "House" is in dict, keep
    "Gold": "金币",         # keep! "Gold" is useful for "N Gold" -> "N 金币"
    "None": "无",           # too short, too common
    "hp": "生命值",         # too short
    "Mods": "模组",         # might match inside words
    "Onyx": "玛瑙",         # might match inside words
    "Puce": "褐紫",         # might match inside words
    "Inn": "旅馆",          # too short, might match inside words - but it's in dict already as HIT
}

# Actually, let me be more selective. Only remove the ones that ACTUALLY caused problems
# in rollback.log. Keep the rest - the new longest-match-first algorithm should handle
# most issues by matching longer phrases first.
harmful_confirmed = {
    "Sea": "瑞恩伯",        # On Research -> On Re瑞恩伯rch (CONFIRMED in rollback.log)
    "Building": "正在建造",  # Buildings -> 正在建造s (CONFIRMED)
    "Wizards": "法师",      # Wizards Tower -> 法师s Tower (CONFIRMED)
    "Heal": "治疗",         # Healing -> 治疗ing (potential)
    "Vine": "藤",           # Vines -> 藤s (potential)
    "Stone": "石化",       # Stones -> 石化s (potential)
    "Vigil": "守夜",        # Vigilance -> 守夜ance (potential)
    "Vigor": "活力",        # Vigorous -> 活力ous (potential)
    "Gnome": "地精",        # might match (potential)
    "Dwarf": "矮人",        # Dwarven -> 矮人en (potential)
    "Harpy": "鹰身女妖",   # Harpies -> 鹰身女妖ies (potential)
    "Curse": "诅咒",        # Cursed -> 诅咒d (potential)
    "Tree": "格莱德",      # Trees -> 格莱德s (potential)
    "Vines": "藤蔓术",      # Vines in other contexts (potential)
    "Mace": "钉头锤",       # matches "Macedonia" etc (unlikely but short)
    "Lich": "巫妖",         # Liche -> 巫妖e (potential)
    "Rogue": "女盗贼",      # Rogues -> 女盗贼s (potential)
    "Empty": "空的",        # Emptied -> 空的ied (potential)
    "Off": "关闭",          # Office -> 关闭ice (potential)
    "Yes": "是",            # "Yesterday" -> "是terday" (potential)
    "hp": "生命值",         # "php" etc (unlikely but 2 chars)
    "Mods": "模组",         # "Modscan" etc (unlikely)
    "Onyx": "玛瑙",         # unlikely
    "Puce": "褐紫",         # unlikely
    "Snake": "西里",        # "Snakelike" -> "西里like" (potential)
    "Solar": "太阳骑士",    # "Solarium" -> "太阳骑士ium" (potential)
    "Monk": "僧侣",         # "Monkey" -> "僧侣ey" (potential)
    "Moss": "苔",           # "Mossy" -> "苔y" (potential)
    "Mist": "薄雾",         # "Mister" -> "薄雾ter" (potential)
    "Smoke": "烟雾",        # "Smokescreen" -> "烟雾screen" (potential)
}

removed = 0
for k in harmful_confirmed:
    if k in rb:
        del rb[k]
        removed += 1

print(f"Removed {removed} harmful short words")
print(f"Final rollback.json: {len(rb)} entries")

# Write
with open(rb_path, "w", encoding="utf-8") as f:
    json.dump(rb, f, ensure_ascii=False, indent=2, sort_keys=True)

# Verify
with open(rb_path, "r", encoding="utf-8") as f:
    verify = json.load(f)
print(f"Verified: {len(verify)} entries")

# Show some long phrases that were added
print("\nLong phrases added (length >= 15):")
long_added = {k:v for k,v in verify.items() if len(k) >= 15 and k not in harmful_confirmed}
for k, v in sorted(long_added.items(), key=lambda x: len(x[0]), reverse=True)[:10]:
    print(f"  [{len(k)}] {k!r} -> {v!r}")
