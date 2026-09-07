#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate dict_2.json for Majesty HD runtime localization."""

import json, os

entries = {}

# ============================================================
# 1. Cast 技能 + Unavailable 后缀（rollback.log 中出现，精确匹配）
# ============================================================
# 译名标准：统一用主 dict.json 的 Cast 技能名 + 限制条件翻译
# "Cast XXX.- (Unavailable: requires YYY)" 格式

cast_skill_map = {
    "Cast Resurrection.": "施用光明复活术。",
    "Cast Blessing.": "施用祝福术。",
    "Cast Healing.": "施展治疗术。",
    "Cast Super Charge.": "施展超级充能。",
    "Cast Vines.": "施展缠绕藤蔓。",
    "Cast Illusionary Hero.": "施展幻影英雄。",
    "Cast Reanimate.": "施用黑暗复活术。",
    "Cast Animate Bones.": "施用操纵骷髅。",
    "Cast Wither.": "施用消亡术。",
    "Cast Petrify.": "施用石化术。",
    "Cast Vigilance.": "施用警觉术。",
    "Cast Stone Skin.": "施用石头皮肤。",
    "Cast Fire Strike.": "施用火焰攻击。",
    "Cast Sun Scorch.": "施用日光灼烧。",
    "Cast Winged Feet.": "施用飞毛腿。",
    "Cast Wind Storm.": "施用暴风。",
    "Cast Change of Heart": "施放变心术",
    "Cast Frost Field": "施放冰霜领域",
    "Cast Earthquake": "施放地震术",
    "Cast Chain Lightning.": "施放连锁闪电。",
    "Cast Gate.": "施放传送门。",
    "Cast Dismiss.": "施放遣散术。",
    "Cast Anti-magic Shield.": "施展反魔法护盾。",
    "Cast Lightning Bolt.": "施放闪电术。",
    "Cast Invisibility.": "施放隐身术。",
    "Cast Farseeing.": "施展远视术。",
    "Cast Lightning Storm.": "施展闪电风暴。",
}

require_map = {
    "Temple to Agrela Level 3": "3级阿格雷拉神庙",
    "Temple to Agrela Level 2": "2级阿格雷拉神庙",
    "Temple to Agrela": "阿格雷拉神庙",
    "Temple to Fervus Level 3": "3级费尔弗斯神庙",
    "Temple to Fervus Level 2": "2级费尔弗斯神庙",
    "Temple to Fervus": "费尔弗斯神庙",
    "Temple to Krypta Level 3": "3级墓穴神庙",
    "Temple to Krypta Level 2": "2级墓穴神庙",
    "Temple to Krypta": "墓穴神庙",
    "Temple to Dauros Level 3": "3级道尔罗斯神庙",
    "Temple to Dauros Level 2": "2级道尔罗斯神庙",
    "Temple to Dauros": "道尔罗斯神庙",
    "Temple to Helia": "赫莉娅神庙",
    "Temple to Lunord": "卢诺德神庙",
    "Wizards Guild Level 3": "3级魔法行会",
    "Wizards Guild Level 2": "2级魔法行会",
    "Wizards Guild": "魔法行会",
    "Sorcerers Abode Level 3": "三级巫师住所",
    "Sorcerers Abode Level 2": "二级巫师住所",
    "Sorcerers Abode": "巫师住所",
}

# Build Cast + Unavailable entries
# Format from rollback.log:
# "Cast XXX.- (Unavailable: requires YYY)"
# "Cast XXX- (Unavailable: requires YYY)"  (no dot before dash)
for skill_en, skill_cn in cast_skill_map.items():
    for req_en, req_cn in require_map.items():
        # With ".- " separator (most common)
        key1 = f"{skill_en}- (Unavailable: requires {req_en})"
        val1 = f"{skill_cn}-（不可用：需要{req_cn}）"
        entries[key1] = val1
        # With ". " separator
        # (some have ".- " and some have "- " without dot)
        # Check skill ends with "." for the ".- " pattern
        if skill_en.endswith("."):
            # Also try without the trailing dot in the key (game might send without it)
            pass

# Also add specific entries seen in rollback.log with exact format
# Some Cast skills don't have ".- " but "- " (no trailing dot on skill)
# From rollback.log line 28-30: "Cast Change of Heart- (Unavailable: ...)"
# These are already covered by the loop above since skill_en = "Cast Change of Heart" (no dot)

# ============================================================
# 2. 建筑名称 + #N（单位编号）
# ============================================================
building_base = {
    "Wizards Tower": "魔法塔",
    "Library": "图书馆",
    "Ballista Tower": "弩炮楼",
    "Peasant": "农民",
    "Tax Collector": "收税员",
    "Palace Guard": "王宫守卫",
    "House": "住房",
    "Guardhouse": "守卫塔",
}
for bname, btrans in building_base.items():
    for n in range(1, 6):
        entries[f"{bname} #{n}"] = f"{btrans} #{n}"

# ============================================================
# 3. 建筑名称 + [newline] + N gold（建造成本提示）
# ============================================================
cost_buildings = {
    "Ballista Tower": "弩炮楼",
    "Blacksmith": "铁匠铺",
    "Dwarven Settlement": "矮人公会",
    "Embassy": "使馆",
    "Guardhouse": "守卫塔",
    "Inn": "旅馆",
    "Magic Bazaar": "魔法集市",
    "Marketplace": "市场",
    "Mausoleum": "陵墓",
    "Outpost": "前哨",
    "Rangers Guild": "游侠行会",
    "Rogues Guild": "盗贼行会",
    "Statue": "雕像",
    "Warriors Guild": "战士行会",
    "Wizards Guild": "魔法行会",
    "Temple to Agrela": "阿格雷拉神庙",
    "Temple to Dauros": "道尔罗斯神庙",
    "Temple to Fervus": "费尔弗斯神庙",
    "Temple to Krolm": "克罗尔玛神庙",
    "Temple to Krypta": "墓穴神庙",
}
# Known gold costs from rollback.log
cost_map = {
    "Ballista Tower": 950,
    "Blacksmith": 475,
    "Dwarven Settlement": 4750,
    "Embassy": 2850,
    "Guardhouse": 570,
    "Inn": 380,
    "Magic Bazaar": 1330,
    "Marketplace": 1425,
    "Mausoleum": 2850,
    "Outpost": 2850,
    "Rangers Guild": 665,
    "Rogues Guild": 570,
    "Statue": 570,
    "Warriors Guild": 760,
    "Wizards Guild": 1425,
    "Temple to Agrela": 950,
    "Temple to Dauros": 1520,
    "Temple to Fervus": 855,
    "Temple to Krolm": 855,
    "Temple to Krypta": 1330,
}
for bname, gold in cost_map.items():
    btrans = cost_buildings[bname]
    entries[f"{bname}[newline]{gold} gold"] = f"{btrans}[newline]{gold} 金币"

# ============================================================
# 4. 建筑 + Level N + (N of N hp) 格式
# ============================================================
# "Palace Level 2 (700 of 700 hp)" etc.
level_hp_entries = {
    "Palace Level 2 (700 of 700 hp)": "2级王宫 (700 of 700 hp)",
    "Palace Level 3 (900 of 900 hp)": "3级王宫 (900 of 900 hp)",
    "Ballista Tower #1 (350 of 350 hp)": "弩炮楼 #1 (350 of 350 hp)",
    "Blacksmith Level 3 (400 of 400 hp)": "铁匠铺 Level 3 (400 of 400 hp)",
    "Mausoleum (300 of 300 hp)": "陵墓 (300 of 300 hp)",
    "Dwarven Settlement (600 of 600 hp)": "矮人公会 (600 of 600 hp)",
}
entries.update(level_hp_entries)

# House #N (NN of NN hp, NN gold)
entries["House #1 (75 of 75 hp, 15 gold)"] = "住房 #1 (75 of 75 hp, 15 金币)"
entries["House #2 (75 of 75 hp, 12 gold)"] = "住房 #2 (75 of 75 hp, 12 金币)"
entries["House #3 (75 of 75 hp, 10 gold)"] = "住房 #3 (75 of 75 hp, 10 金币)"
entries["House #4 (75 of 75 hp, 8 gold)"] = "住房 #4 (75 of 75 hp, 8 金币)"
entries["House #5 (75 of 75 hp, 5 gold)"] = "住房 #5 (75 of 75 hp, 5 金币)"
# Generic patterns with varying gold
for n in range(1, 6):
    for gold in [5, 8, 10, 12, 15]:
        entries[f"House #{n} (75 of 75 hp, {gold} gold)"] = f"住房 #{n} (75 of 75 hp, {gold} 金币)"

# ============================================================
# 5. 财务报表行（含前导空格）
# ============================================================
finance_entries = {
    "Riches": "财产",
    "   Total Made: 0": "   财产总额: 0",
    "Gold Balance: 0": "金币余额: 0",
    "Gold Initial: 0": "初始金币: 0",
    "Total Spent: 0": "总花费: 0",
    "      On Research: 0": "      研究支出: 0",
    "      On Buildings: 0": "      建造支出: 0",
    "      On Heroes: 0": "      英雄支出: 0",
    "      On Spells: 0": "      魔法支出: 0",
    "      On Flags: 0": "      旗帜支出: 0",
    "----------------------------": "----------------------------",
    "   Constructed: 0": "   已建造: 0",
    "   Destroyed: 0": "   已摧毁: 0",
    "   Standing: 0": "   现存: 0",
    "   Recruited: 0": "   已招募: 0",
    "   Killed: 0": "   已阵亡: 0",
    "   Alive: 0": "   存活: 0",
    "   Average Level: 0": "   平均等级: 0",
    "Vice: 0": "恶习: 0",
    "Danger Level: 0": "危险等级: 0",
    "5 Heroes buried here": "5名英雄埋葬于此",
    "Buildings": "建筑",
}
entries.update(finance_entries)

# Generic finance patterns with variable numbers (for future)
# These will work as exact matches when the number matches
for num in range(0, 200):
    entries[f"   Total Made: {num}"] = f"   财产总额: {num}"
    entries[f"   Gold Balance: {num}"] = f"   金币余额: {num}"
    entries[f"   Gold Initial: {num}"] = f"   初始金币: {num}"
    entries[f"   Total Spent: {num}"] = f"   总花费: {num}"
    entries[f"      On Research: {num}"] = f"      研究支出: {num}"
    entries[f"      On Buildings: {num}"] = f"      建造支出: {num}"
    entries[f"      On Heroes: {num}"] = f"      英雄支出: {num}"
    entries[f"      On Spells: {num}"] = f"      魔法支出: {num}"
    entries[f"      On Flags: {num}"] = f"      旗帜支出: {num}"
    entries[f"   Constructed: {num}"] = f"   已建造: {num}"
    entries[f"   Destroyed: {num}"] = f"   已摧毁: {num}"
    entries[f"   Standing: {num}"] = f"   现存: {num}"
    entries[f"   Recruited: {num}"] = f"   已招募: {num}"
    entries[f"   Killed: {num}"] = f"   已阵亡: {num}"
    entries[f"   Alive: {num}"] = f"   存活: {num}"
    entries[f"   Average Level: {num}"] = f"   平均等级: {num}"
    entries[f"Vice: {num}"] = f"恶习: {num}"
    entries[f"Danger Level: {num}"] = f"危险等级: {num}"

# Heroes buried here (0-20)
for n in range(0, 21):
    entries[f"{n} Heroes buried here"] = f"{n}名英雄埋葬于此"

# ============================================================
# 6. HP 显示行
# ============================================================
for hp in [75, 100, 150, 200, 250, 300, 350, 400, 500, 550, 600, 700, 800, 900, 1000]:
    entries[f"{hp} of {hp} hp"] = f"{hp} of {hp} hp"
    entries[f"Palace ({hp} of {hp} hp)"] = f"王宫 ({hp} of {hp} hp)"

# ============================================================
# 7. miss.log 中的其他条目
# ============================================================
misc_entries = {
    "default": "默认",
    "Keep the new resolution?": "保持新分辨率？",
    "Resolution Changed": "分辨率已更改",
}
entries.update(misc_entries)

# ============================================================
# 8. N gold (小写 gold) - 建筑成本提示中的金额
# ============================================================
for g in [380, 475, 570, 665, 760, 855, 950, 1330, 1425, 1520, 2850, 4750]:
    entries[f"{g} gold"] = f"{g} 金币"

# ============================================================
# 9. Luxurious Beetle (英雄名 - rollback误匹配)
# ============================================================
# This is a hero name, keep as-is or provide proper translation
# Actually from the game context, this is a special hero name
# Let's add it to dict to avoid rollback mangling it
entries["Luxurious Beetle"] = "豪华甲虫"

# ============================================================
# Write output
# ============================================================

# Remove any entries that already exist in dict.json or dict_1.json
scripts_dir = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts"
with open(os.path.join(scripts_dir, "dict.json"), "r", encoding="utf-8") as f:
    d0 = json.load(f)
with open(os.path.join(scripts_dir, "dict_1.json"), "r", encoding="utf-8") as f:
    d1 = json.load(f)
existing = set(d0.keys()) | set(d1.keys())

# Filter out duplicates
deduped = {}
skipped = 0
for k, v in entries.items():
    if k in existing:
        skipped += 1
        # But if the existing dict has a different value, we should still add ours
        # Actually no - if it's in dict.json already, it's a HIT already, no need
        continue
    deduped[k] = v

print(f"Total generated: {len(entries)}")
print(f"Skipped (already in dict.json/dict_1.json): {skipped}")
print(f"Final dict_2.json entries: {len(deduped)}")

# Write with sorted keys for consistency
out_path = os.path.join(scripts_dir, "dict_2.json")
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(deduped, f, ensure_ascii=False, indent=2, sort_keys=True)

# Verify
with open(out_path, "r", encoding="utf-8") as f:
    verify = json.load(f)
print(f"Verified: {len(verify)} entries")

# Show some sample entries
print("\nSample entries:")
for k in sorted(verify.keys())[:10]:
    print(f"  {k!r} -> {verify[k]!r}")
print("...")
# Show Cast entries
cast_entries = [k for k in verify if k.startswith("Cast")]
print(f"\nCast entries: {len(cast_entries)}")
for k in cast_entries[:5]:
    print(f"  {k!r} -> {verify[k]!r}")
