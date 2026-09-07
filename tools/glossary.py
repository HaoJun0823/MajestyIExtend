#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
M1 HD 汉化 - 术语表（以 M2 译名为准）

本表用于将原版中文译名统一到 M2 标准。
同时也包含新增翻译需要用到的术语。

格式: {
    'english': 'm2_chinese',
    ...
}
"""

# 神明/神殿（M2 译名）
DEITY = {
    'Agrela': '阿格雷拉',
    'Dauros': '道罗斯',
    'Fervus': '费尔弗斯',
    'Helia': '赫莉娅',
    'Krolm': '克罗尔玛',
    'Krypta': '墓穴',
    'Lunord': '卢诺德',
    'Grum-Gog': '格鲁姆-古格',
}

# 神殿全称
TEMPLE = {
    'TEMPLE TO AGRELA': '阿格雷拉神殿',
    'TEMPLE TO DAUROS': '道罗斯神殿',
    'TEMPLE TO FERVUS': '费尔弗斯神殿',
    'TEMPLE TO HELIA': '赫莉娅神殿',
    'TEMPLE TO KROLM': '克罗尔玛神殿',
    'TEMPLE TO KRYPTA': '墓穴神殿',
    'TEMPLE TO LUNORD': '卢诺德神殿',
}

# 英雄职业（M2 译名）
HERO_CLASS = {
    'Warrior': '战士',
    'Ranger': '游侠',
    'Rogue': '女盗贼',
    'Wizard': '法师',
    'Paladin': '圣骑士',
    'Dwarf': '矮人',
    'Elf': '精灵',
    'Cleric': '见习修女',
    'Blademaster': '剑圣',
    'Beastmaster': '驯兽师',
    'Marksman': '弩手',
    'Barbarian': '野蛮人',
    'Monk': '僧侣',
    'Adept': '信徒',
    'Solar': '太阳骑士',
    'Solarii': '索拉里',
    'Warrior of Discord': '不和谐战士',
}

# 建筑名称（M2 译名）
BUILDING = {
    'BLACKSMITH': '铁匠铺',
    'MARKETPLACE': '市集',
    'FAIRGROUNDS': '竞技场',
    'GUARDHOUSE': '守卫塔',
    'WIZARDS GUILD': '法师公会',
    'WARRIORS GUILD': '战士公会',
    'RANGERS GUILD': '游侠公会',
    'ROGUES GUILD': '女盗贼公会',
    'PALADINS GUILD': '圣骑士公会',
    'DWARVEN SETTLEMENT': '矮人公会',
    'ELVEN HALL': '精灵公会',
    'INN': '酒馆',
    'LIBRARY': '图书馆',
    'GRAVEYARD': '墓地',
    'CASINO': '赌场',
    'BAZAAR': '集市',
    'EMBASSY': '使馆',
    'WATCHTOWER': '瞭望塔',
    'PALACE': '宫殿',
    'CASTLE': '城堡',
}

# 怪物（M2 译名）
MONSTER = {
    'Goblin': '哥布林',
    'Goblin Spearman': '哥布林矛兵',
    'Skeleton': '骷髅',
    'Zombie': '丧尸',
    'Dragon': '巨龙',
    'Minotaur': '牛头人',
    'Werewolf': '狼人',
    'Vampire': '吸血鬼',
    'Giant Spider': '巨型蜘蛛',
    'Medusa': '美杜莎',
    'Demon': '恶魔',
    'Ratman': '鼠人',
    'Troll': '巨魔',
    'Harpy': '鹰身女妖',
    'Wraith': '怨灵',
    'Gargoyle': '石像鬼',
    'Bugbear': '熊地精',
    'Lich': '巫妖',
    'Firedrake': '火龙',
    'Rockman': '石头人',
    'Yeti': '雪人',
    'Unicorn': '独角兽',
}

# 魔法/技能（M2 译名）
SPELL = {
    'Lightning Bolt': '闪电',
    'Heal': '治疗',
    'Healing': '治疗',
    'Resurrect': '复活',
    'Krolm\'s Rage': '克罗尔玛之怒',
    'The Wrath of Krolm': '克罗尔玛之怒',
    'Sunray': '太阳射线',
    'Vampirism': '吸血',
    'Skeletonize': '骷髅化',
    'Shock Blast': '冲击波',
    'Storm': '风暴',
    'Warding': '守护',
    'Empowering': '赋能',
    'Flame': '火焰',
    'Ice': '冰冻',
    'Poison': '毒药',
    'Slow': '减速',
    'Haste': '加速',
    'Stone': '石化',
    'Fear': '恐惧',
    'Compassion': '怜悯',
    'Vengeance': '复仇',
    'Meteor': '陨石',
    'Summon': '召唤',
    'Blessing': '祝福',
    'Curse': '诅咒',
    ', the Fist of Dauros': '道罗斯之拳',
}

# UI 通用词汇（M2 译名）
UI = {
    'ACCEPT': '应用',
    'OK': '确定',
    'CANCEL': '取消',
    'CONFIRM': '确认',
    'HEROES': '英雄',
    'VISITORS': '访客',
    'Visitors': '访客',
    'MESSAGE': '消息',
    'LOAD GAME': '加载游戏',
    'MULTIPLAYER': '多人模式',
    'CREDITS': '关于作者',
    'MAIN MENU': '菜单',
    'OPTIONS': '设置',
    'SPELLS': '法术',
    'BUILDINGS': '建筑',
    'MINI-MAP': '袖珍地图',
    'MAIN MAP': '主地图',
    'TRACKING WINDOW': '跟踪视窗',
    'RANDOM': '随缘',
    'ADVANCED': '老兵',
    'Name': '名称',
}

# M1 原版中文 -> M2 中文 的替换映射
# 用于将原版翻译中的术语替换为 M2 标准
TERM_REPLACE = {
    # 神明
    '安格瑞拉': '阿格雷拉',
    '弗尔瓦斯': '费尔弗斯',
    '海尔利亚': '赫莉娅',
    '克罗尔姆': '克罗尔玛',
    '克莱普塔': '墓穴',
    '鲁恩纳德': '卢诺德',
    # 神殿
    '安格瑞拉寺庙': '阿格雷拉神殿',
    '弗尔瓦斯神庙': '费尔弗斯神殿',
    '海尔利亚神庙': '赫莉娅神殿',
    '克罗尔姆神庙': '克罗尔玛神殿',
    '克莱普塔神庙': '墓穴神殿',
    '鲁恩纳德神庙': '卢诺德神殿',
    # 建筑
    '警卫塔楼': '守卫塔',
    '矮人驻地': '矮人公会',
    '来访者': '访客',
    '信息': '消息',
    '载入游戏': '加载游戏',
    '制作人员名单': '关于作者',
    '多人游戏': '多人模式',
    '主菜单': '菜单',
    # UI
    '选项': '设置',
    '袖珍地图': '袖珍地图',
    '跟踪视窗': '跟踪视窗',
    '主地图': '主地图',
    # 英雄职业
    '流浪者': '游侠',
    '盗贼': '女盗贼',
    '巫师': '法师',
    '圣武士': '圣骑士',
}

# 所有术语合并（英文->中文），用于新翻译参考
ALL_TERMS = {}
for d in [DEITY, TEMPLE, HERO_CLASS, BUILDING, MONSTER, SPELL, UI]:
    ALL_TERMS.update(d)


if __name__ == '__main__':
    print(f"Deity terms: {len(DEITY)}")
    print(f"Temple terms: {len(TEMPLE)}")
    print(f"Hero class terms: {len(HERO_CLASS)}")
    print(f"Building terms: {len(BUILDING)}")
    print(f"Monster terms: {len(MONSTER)}")
    print(f"Spell terms: {len(SPELL)}")
    print(f"UI terms: {len(UI)}")
    print(f"Term replacements: {len(TERM_REPLACE)}")
    print(f"Total terms: {len(ALL_TERMS)}")
