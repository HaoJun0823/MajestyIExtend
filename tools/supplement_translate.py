#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
第二遍翻译：补充所有未翻译条目

分类处理:
1. 调试器/开发工具文本 -> 不翻译或简单翻译
2. 英雄称号 -> 音译+意译
3. 地图名 -> 意译
4. 任务名 -> 意译（对齐 M2）
5. 任务物品 -> 意译
6. 任务结局 -> 意译
7. 游戏消息 -> 意译
8. UI 字符串 -> 意译
9. 错误消息 -> 意译
10. 帮助文本 -> 意译
"""
import json
import os

# 加载未翻译条目
untrans_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\untranslated.json'
with open(untrans_path, 'r', encoding='utf-8') as f:
    items = json.load(f)

# ============================================================
# 补充翻译表
# ============================================================
SUPPLEMENT_TRANSLATIONS = {
    # --- 月份 ---
    "February": "二月",
    "March": "三月",
    
    # --- 热键 ---
    "Ctrl": "Ctrl",
    "Shift": "Shift",
    
    # --- 单位名 ---
    "Lorphus (Jabberwok)": "洛弗斯（炸龙）",
    
    # --- 调试器（不翻译，仅开发用） ---
    "Step Over": "Step Over",
    "Step Out": "Step Out",
    "Debugger": "调试器",
    "Profiler": "性能分析器",
    "Xml Data": "XML 数据",
    "Global Identifier Management": "全局标识管理",
    "C-Attribs.": "C 属性",
    "Game Object and Script Tracking": "游戏对象与脚本跟踪",
    "Attributes": "属性",
    
    # --- UI 字符串 ---
    "Select a new debug action": "选择新的调试操作",
    "Strength - how strong this hero is": "力量 - 该英雄的力量值",
    "CCEPT": "接受",  # AP35 的截断文本
    "ist of kingdom statistics": "王国统计列表",  # AP40 的截断文本
    "Cast Invisibility.": "施放隐身术。",
    "Cast Lightning Bolt.": "施放闪电术。",
    "Toggle this building on or off tax route.": "将此建筑加入或移出税收路线。",
    "Please enter the Host Name and Port Number of the game you want to join.": "请输入你要加入的游戏的主机名和端口号。",
    
    # AP30 任务选择界面
    "The Dark Forest": "暗影森林",
    "Day of Reckoning": "清算之日",
    "Tomb of the Dragon King": "龙之王陵",
    "Elven Treachery": "精灵的背叛",
    "The Fertile Plains": "丰饶平原",
    "The Forsaken Lands": "被遗弃之地",
    "Goblin Hordes": "哥布林大军",
    "The Liche Queen": "巫妖女王",
    "Save the Prince": "拯救王子",
    "The Magic Ring": "魔法戒指",
    "Slave Pits": "奴隶坑",
    "THe Wizard's Curse": "巫师的诅咒",
    "Brashnard's Sphere": "布拉什纳德之球",
    "Deal with the Demon": "恶魔交易",
    "Slay the Mighty Dragon": "屠龙",
    "holy chalice": "圣杯",
    "tomb of dragon king": "龙之王陵",
    "day of reckoning": "清算之日",
    "elven treachery": "精灵的背叛",
    "frame": "框架",
    "Placeholder Revolving Quest Pog": "占位旋转任务图标",
    "Current Quest Name": "当前任务名称",
    "num    easy medium hard number of times played              high score etc      ": "数量  简单 中等 困难 游玩次数              最高分等      ",
    "quest notes": "任务备注",
    "ERASE VICTORIES": "清除通关记录",
    "Freestyle Game": "自由模式",
    "Cheat All Quests": "解锁全部任务",
    "FREESTYLE QUESTS": "自由模式任务",
    "Bonus Downloadable Quest 1 Trigger": "奖励下载任务1触发器",
    
    # --- 游戏消息 ---
    " is ": " 是 ",
    "Tonic of Speed": "加速药水",
    "Firebalm": "火焰香膏",
    "Dirgo Strength": "力量药剂",
    "Regeneration": "再生",
    "Invisibility": "隐身",
    "Shapeshift Tincture": "变形药剂",
    "No more than %d of this type are permitted": "此类型最多允许%d个",
    "No buildings of this type are permitted": "不允许建造此类型建筑",
    " (Limit %d)": " (上限%d)",
    "No more than %d upgrades of this type are permitted": "此类型升级最多允许%d个",
    "No upgrades of this type are permitted": "不允许此类型升级",
    "PLAYER FORCES": "我方兵力",
    "ENEMY FORCES": "敌方兵力",
    
    # --- 英雄称号 (HN59) ---
    " Broadedge": " 宽刃",
    " Headlopper": " 砍头者",
    " Keenswing": " 锋挥",
    " Knightly": " 骑士风",
    " of the Bloody Blade": " 血刃之",
    " Sharpwit": " 敏智",
    " Silveredge": " 银刃",
    " Solidfist": " 铁拳",
    ", Son of Kolar": "，科拉之子",
    " Stoneblade": " 石刃",
    " Strongwill": " 强志",
    " Swiftblade": " 快刃",
    " the Battlemaster": " 战斗大师",
    " the Blue": " 蓝色之",
    " the Courageous": " 勇者之",
    " the Loyal": " 忠诚之",
    " the Slicer": " 切割者",
    " the Victorious": " 胜利者之",
    " the Worthy": " 无愧者之",
    " of Longridge": " 长岭之",
    " Vunderson": " 范德森",
    " Quickparry": " 快格",
    ", the Fairly Swift": "，颇为迅速者",
    ", the Mighty": "，强者之",
    " of Kranal's Creek": " 克拉纳尔溪之",
    " the Brute": " 蛮横之",
    " the Great": " 伟大之",
    " the Fair": " 公正之",
    " of the Many Colours": " 多彩之",
    " the Cunning": " 狡黠之",
    " Goldentongue": " 金舌",
    " of the West": " 西方之",
    " the Bold": " 大胆之",
    " the Just": " 正义之",
    " of the Fallen Deer": " 坠鹿之",
    " of Ardenwood": " 阿登林之",
    " of the North": " 北境之",
    " the Lionslayer": " 狮杀者",
    " the Dragonslayer": " 屠龙者",
    " the Giantslayer": " 巨人杀者",
    " the Braveling": " 勇者",
    " the Griffon Talon": " 狮鹫之爪",
    " the Wolf": " 狼",
    " the Mad": " 疯狂之",
    " the Pale": " 苍白之",
    " of the Sun": " 太阳之",
    " the Shrewd": " 精明之",
    " the Seeker": " 探寻者",
    
    # --- 资料片英雄名 (HN58) ---
    " Duvane": " 杜凡",
    " Nelton": " 内尔顿",
    " Dremenon": " 德雷梅农",
    " Norvus": " 诺弗斯",
    " Bhyll": " 比尔",
    " Broadedge": " 宽刃",
    " Tremon": " 特雷蒙",
    " Valian": " 瓦利安",
    " Willem": " 威廉",
    " Talon": " 塔隆",
    " Gerwyn": " 格温",
    " Kael": " 凯尔",
    " Pheron": " 费隆",
    " Reth": " 雷斯",
    " Tarn": " 塔恩",
    " Vex": " 维克斯",
    " Zarek": " 扎雷克",
    " Dall": " 达尔",
    " Fenris": " 芬里斯",
    " Varg": " 瓦尔格",
    " Skarn": " 斯卡恩",
    
    # --- 任务物品 (QITM) ---
    "empty": "空",
    "Speed Tonic\nFFDDAA(Temporary Speed Boost)": "加速药水\nFFDDAA（临时加速）",
    "Fire Balm\nFFDDAA(Temporarily Ignites Weapons)": "火焰香膏\nFFDDAA（临时点燃武器）",
    "Strength Potion\nFFDDAA(Temporary Strength Boost)": "力量药剂\nFFDDAA（临时力量提升）",
    "Regeneration Elixer\nFFDDAA(Temporary Healing Boost)": "再生灵药\nFFDDAA（临时治疗提升）",
    "Invisibility Brew\nFFDDAA(Temporary Invisibility)": "隐身药剂\nFFDDAA（临时隐身）",
    "Shapeshift Potion\nFFDDAA(Temporary Transformation)": "变形药水\nFFDDAA（临时变形）",
    "Bracers of Immolation\nFFDDAA(Damage Shield)": "自燃护腕\nFFDDAA（伤害护盾）",
    "Belt of Reflection\nFFDDAA(Magic Shield)": "反射腰带\nFFDDAA（魔法护盾）",
    "The Teevus Wand\nFFDDAA(Attack Spell)": "蒂乌斯魔杖\nFFDDAA（攻击法术）",
    "Rune of Healing\nFFDDAA(Healing Spell)": "治疗符文\nFFDDAA（治疗法术）",
    "Helm of Displacement\nFFDDAA(Attack Spell)": "位移头盔\nFFDDAA（攻击法术）",
    
    # --- 任务名 (QUES) ---
    "The Clash of Empires": "帝国冲突",
    "Darkness Falls": "黑暗降临",
    "The Fortress of Ixmil": "伊克斯米尔要塞",
    "Legendary Heroes": "传奇英雄",
    "Rise of the Ratmen": "鼠人崛起",
    "Scions of Chaos": "混沌之子",
    "The Siege": "围城",
    "Spires of Death": "死亡之塔",
    "Trade Routes": "贸易路线",
    "Urban Renewal": "城市重建",
    "The Valley of the Serpents": "毒蛇之谷",
    "Vigil for a Fallen Hero": "陨落英雄的守夜",
    "Balance of Twilight": "暮光之衡",
    "Bonus Quest 2": "奖励任务2",
    "Bonus Quest 3": "奖励任务3",
    
    # --- 任务结局 (QEND) ---
    "Well done, Your Majesty. With the defeat of Krolm's Avatar, his influence over the realm has been broken.": "干得漂亮，陛下。随着克罗尔玛化身被击败，他对这片领域的影响力已被打破。",
    "Congratulations!  You have won the game.": "恭喜！你赢得了游戏。",
    "At last our streets are peaceful again... albeit drenched in Goblin spittle and blood.": "我们的街道终于恢复了和平……尽管浸透了哥布林的唾液和鲜血。",
    "At last, Styx AND Stones are dead. With the destruction of this disruptive presence, the northern trade routes are secure once more.": "斯堤克斯和斯通斯终于都死了。随着这个破坏性存在的覆灭，北方贸易路线再次安全了。",
    
    # --- 地图名 (FNTX) ---
    "Dead Lands (Advanced) - S": "死亡之地（进阶）- 小",
    "Frozen Wrath (Expert)": "冰霜之怒（大师）",
    "Lords of the North (Advanced)": "北境领主（进阶）",
    "Necropolis (Advanced) - S": "死灵之都（进阶）- 小",
    "Northern Kings (Expert)": "北方之王（大师）",
    "Outposts (Expert)": "前哨（大师）",
    "Ravenous (Expert) - L": "贪婪者（大师）- 大",
    "Rogues Haven (Advanced)": "盗贼天堂（进阶）",
    "Ruinous Domain (Expert) - S": "破败领地（大师）- 小",
    "Vermin Assault (Advanced)": "害兽突袭（进阶）",
    "Wild Expanse (Advanced) - H": "荒野广袤（进阶）- 高",
    "Winter Behemoths (Expert) - L": "冬日巨兽（大师）- 大",
    "Wizard Lords (Beginner)": "法师领主（新手）",
    "Yeti Rampage (Expert) - S": "雪人暴走（大师）- 小",
    "Champions of Discord (Advanced)": "不和谐冠军（进阶）",
    "Shanty Town (Expert)": "棚户镇（大师）",
    "Arcane Ally (Advanced)": "奥术盟友（进阶）",
    "Unholy Strife (Advanced)": "邪恶纷争（进阶）",
    "Conclave of Shamans (Advanced) - L": "萨满议会（进阶）- 大",
    "Tundra Raiders (Expert) - L ": "冻原劫掠者（大师）- 大 ",
    "Gorgon Onslaught (Advanced)": "戈贡突袭（进阶）",
    "Shadow Beasts (Expert)": "暗影兽（大师）",
    "Arcane Struggle (Advanced)": "奥术之争（进阶）",
    "Ruins of Esterhausen (Advanced) - S": "埃斯特豪森废墟（进阶）- 小",
    "Serpent Assault (Expert) - L": "毒蛇突袭（大师）- 大",
    "Dead Lands (Advanced)": "死亡之地（进阶）",
    "Necropolis (Advanced)": "死灵之都（进阶）",
    "Ravenous (Expert)": "贪婪者（大师）",
    "Ruinous Domain (Expert)": "破败领地（大师）",
    "Rogues Haven (Expert)": "盗贼天堂（大师）",
    "Outpost (Expert)": "前哨（大师）",
    "Lords of the North (Expert)": "北境领主（大师）",
    "Northern Kings (Advanced)": "北方之王（进阶）",
    "Frozen Wrath (Advanced)": "冰霜之怒（进阶）",
    "Vermin Assault (Expert)": "害兽突袭（大师）",
    "Wild Expanse (Expert)": "荒野广袤（大师）",
    "Winter Behemoths (Advanced)": "冬日巨兽（进阶）",
    "Wizard Lords (Advanced)": "法师领主（进阶）",
    "Yeti Rampage (Advanced)": "雪人暴走（进阶）",
    "Champions of Discord (Expert)": "不和谐冠军（大师）",
    "Shanty Town (Advanced)": "棚户镇（进阶）",
    "Arcane Ally (Expert)": "奥术盟友（大师）",
    "Unholy Strife (Expert)": "邪恶纷争（大师）",
    "Conclave of Shamans (Expert)": "萨满议会（大师）",
    "Tundra Raiders (Advanced)": "冻原劫掠者（进阶）",
    "Gorgon Onslaught (Expert)": "戈贡突袭（大师）",
    "Shadow Beasts (Advanced)": "暗影兽（进阶）",
    "Arcane Struggle (Expert)": "奥术之争（大师）",
    "Ruins of Esterhausen (Expert)": "埃斯特豪森废墟（大师）",
    "Serpent Assault (Advanced)": "毒蛇突袭（进阶）",
    "Balance of Twilight (Advanced)": "暮光之衡（进阶）",
    "Balance of Twilight (Expert)": "暮光之衡（大师）",
    "Dead Lands (Expert)": "死亡之地（大师）",
    "Necropolis (Expert)": "死灵之都（大师）",
    "Ravenous (Advanced)": "贪婪者（进阶）",
    "Ruinous Domain (Advanced)": "破败领地（进阶）",
    "Rogues Haven (Beginner)": "盗贼天堂（新手）",
    "Outposts (Advanced)": "前哨（进阶）",
    "Vermin Assault (Beginner)": "害兽突袭（新手）",
    "Wild Expanse (Beginner)": "荒野广袤（新手）",
    "Winter Behemoths (Beginner)": "冬日巨兽（新手）",
    "Wizard Lords (Expert)": "法师领主（大师）",
    "Yeti Rampage (Beginner)": "雪人暴走（新手）",
    "Champions of Discord (Beginner)": "不和谐冠军（新手）",
    "Shanty Town (Beginner)": "棚户镇（新手）",
    "Arcane Ally (Beginner)": "奥术盟友（新手）",
    "Unholy Strife (Beginner)": "邪恶纷争（新手）",
    "Conclave of Shamans (Beginner)": "萨满议会（新手）",
    "Tundra Raiders (Beginner)": "冻原劫掠者（新手）",
    "Gorgon Onslaught (Beginner)": "戈贡突袭（新手）",
    "Shadow Beasts (Beginner)": "暗影兽（新手）",
    "Arcane Struggle (Beginner)": "奥术之争（新手）",
    "Ruins of Esterhausen (Beginner)": "埃斯特豪森废墟（新手）",
    "Serpent Assault (Beginner)": "毒蛇突袭（新手）",
    "Dead Lands (Beginner)": "死亡之地（新手）",
    "Necropolis (Beginner)": "死灵之都（新手）",
    "Ravenous (Beginner)": "贪婪者（新手）",
    "Ruinous Domain (Beginner)": "破败领地（新手）",
    "Lords of the North (Beginner)": "北境领主（新手）",
    "Northern Kings (Beginner)": "北方之王（新手）",
    "Frozen Wrath (Beginner)": "冰霜之怒（新手）",
    
    # --- 错误消息 (ErS0) ---
    "This map file needs version %s of the program to load, you have version %s.": "此地图文件需要程序版本%s才能加载，你当前的版本为%s。",
    "This file does not appear to be a map file.": "此文件似乎不是地图文件。",
    "This file is for another great Cyberlore game, not this one.": "此文件适用于另一款Cyberlore游戏，并非本游戏。",
    "An error occurred while reading the file.": "读取文件时发生错误。",
    "Unable to allocate enough memory.": "无法分配足够的内存。",
    "The Quest definition is missing.": "任务定义缺失。",
    "The Quest definition is corrupt.": "任务定义已损坏。",
    
    # --- 帮助文本 HPTX ---
    "- The Avatar of Krolm is a massive figure embodying the power and fury of the god Krolm.": "— 克罗尔玛化身是一个巨大的身影，体现了神明克罗尔玛的力量与愤怒。",
    "- The Altars of Krolm are holy places consecrated solely to his worship. To destroy them is to break the power of Krolm in this region.": "— 克罗尔玛祭坛是专门供奉他的圣地。摧毁它们就能打破克罗尔玛在此地区的力量。",
    
    # --- 游戏消息结尾 ---
    "Krolm's power has proven to be too great for you & his will remains supreme in this land.": "克罗尔玛的力量对你来说过于强大，他的意志在这片土地上至高无上。",
    
    # --- 资料片建筑描述 MX00 等 ---
    "The Blacksmith forges new weapons and armor for your heroes.": "铁匠铺为你的英雄打造新武器和护甲。",
    "Hall Of Champions": "英杰殿",
    "Destroy this Hall of Champions.": "拆除这座英杰殿。",
    "Goto tips and details about this building.": "查看此建筑的提示和详情。",
    "Repair this building once.": "修复此建筑一次。",
    
    # --- GMTX 更多 ---
    "PLAYER FORCES": "我方兵力",
    "ENEMY FORCES": "敌方兵力",
}

# 输出补充翻译
print(f"Supplement translations: {len(SUPPLEMENT_TRANSLATIONS)}")

# 保存为 JSON 供主翻译脚本使用
out_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools\supplement_translations.json'
with open(out_path, 'w', encoding='utf-8') as f:
    json.dump(SUPPLEMENT_TRANSLATIONS, f, ensure_ascii=False, indent=2)
print(f"Saved to: {out_path}")

# 统计：还有多少未翻译
remaining = []
for item in items:
    text = item['text']
    if text not in SUPPLEMENT_TRANSLATIONS:
        remaining.append(item)

print(f"\nRemaining untranslated: {len(remaining)}")
# 按 file 分组
from collections import Counter
file_counts = Counter(item['file'] for item in remaining)
for fname, count in file_counts.most_common(20):
    print(f"  {fname}: {count}")
