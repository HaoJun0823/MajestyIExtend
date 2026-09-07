#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 miss.log 和 rollback.log 提取条目，生成 dict_1.json（精确翻译）
同时清理 rollback.json 中的有害条目
"""
import json, re, os

SCRIPTS = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts"

# ==================== 1. 从 rollback.log 提取需要完整翻译的句子 ====================
# rollback.log 格式: [ROLLBACK] "原文" -> "替换后" (wchars=N)
# 我们需要原文，然后人工翻译

rollback_log = os.path.join(SCRIPTS, "rollback - 副本.log")
entries_from_rollback = []
with open(rollback_log, 'r', encoding='utf-8') as f:
    for line in f:
        m = re.match(r'\[ROLLBACK\] "(.*)" -> ".*" \(wchars=\d+\)', line.strip())
        if m:
            entries_from_rollback.append(m.group(1))

# ==================== 2. 从 miss.log 提取需要翻译的条目 ====================
miss_log = os.path.join(SCRIPTS, "miss - 副本.log")
entries_from_miss = []
with open(miss_log, 'r', encoding='utf-8') as f:
    for line in f:
        m = re.match(r'\[MISS\] "(.*)" \(wide=\d+ chLen=\d+\)', line.strip())
        if m:
            entries_from_miss.append(m.group(1))

print(f"From rollback.log: {len(entries_from_rollback)} entries")
print(f"From miss.log: {len(entries_from_miss)} entries")

# ==================== 3. 人工翻译 ====================
# miss.log 中的简单条目
miss_translations = {
    "No Towers": "无塔楼",
    "One of a Kind": "独一无二",
    "Embassy": "使馆",
    "Mausoleum": "陵墓",
    "Sorcerers Abode": "巫师住所",
    "Sorcerers Abode Level 2": "二级巫师住所",
    "Sorcerers Abode Level 3": "三级巫师住所",
    # 版本号/数字/default/已翻译中文 不需要翻译
    # "Version 1.5.2.24" - 不翻译
    # "陛下" - 已翻译，跳过
    # "20000 Gold" - rollback 处理
    # "default" - 不翻译
    # "820100069" - 不翻译
    # "1" - 不翻译
}

# rollback.log 中的完整句子翻译
# 这些是任务简报/描述文本，需要完整翻译
# [newline] 保持与 dict key 一致
rollback_translations = {
    # ---- 简单条目 ----
    "Brute Force": "蛮力",
    "Wizard War": "法师之战",
    "Magic Bazaar Level 2": "二级魔法集市",
    "Magic Bazaar Level 3": "三级魔法集市",
    
    # ---- 任务描述（含 [newline]） ----
    "A single Ratman settlement has sprung up in this region. If it is not destroyed, more will surely follow.[newline](Many weak to strong monsters present)":
        "一个鼠人聚落在此地区出现。如果不加以消灭，更多的鼠人必将接踵而至。[newline](有大量从弱到强的怪物)",
    
    "A scouting force of Ratmen has occupied several caves in this region.[newline](A few weak to average monsters present)":
        "一支鼠人侦察队占据了此地区的数个洞穴。[newline](有少量从弱到中等的怪物)",
    
    "Hordes of Skeletons roam the region. [newline](Many weak to average monsters)":
        "成群的骷髅在此地区游荡。[newline](有大量从弱到中等的怪物)",
    
    "The region is home to many wild creatures. A site of arcane power is rumored to be guarded by several Daemonwoods. [newline](Many weak to very strong monsters present)":
        "此地区栖息着许多野生生物。据说一处奥术力量之地由数棵树精守护。[newline](有大量从弱到极强的怪物)",
    
    "The region is controlled by several tribes of Goblins.[newline](Many weak to average monsters present)":
        "此地区由数个哥布林部落控制。[newline](有大量从弱到中等的怪物)",
    
    "Some Goblin castaways have set up a small tribe of their own in some local caves. There is evidence that they are trying to breed Giant Spiders. [newline](Many weak monsters)":
        "一些哥布林流亡者在当地洞穴中建立了自己的小部落。有证据表明他们正试图饲养巨型蜘蛛。[newline](有大量弱小的怪物)",
    
    "The restless dead in this region have been making open assaults on Kingdom settlements. [newline](Many weak to strong monsters)":
        "此地区的亡灵不断对王国定居点发起公开袭击。[newline](有大量从弱到强的怪物)",
    
    "The region is overrun by Varg and Werewolves.[newline](Some weak to strong monsters present)":
        "此地区被瓦尔格和狼人所侵占。[newline](有一些从弱到强的怪物)",
    
    "Wendigo, Werewolves, Vargs and Hellbears dominate this huge expanse of wilderness[newline](Many average to strong monsters present)[newline](Use of this force pattern requires 128 meg RAM min)":
        "温迪戈、狼人、瓦尔格和地狱熊统治着这片广袤的荒野[newline](有大量从中等到强力的怪物)[newline](使用此力量模式至少需要128兆内存)",
    
    "Goblins have fortified this region with a single Goblin Fortress and several watch towers.[newline](A few weak to average monsters present)":
        "哥布林用一座哥布林堡垒和数座瞭望塔在此地区设防。[newline](有少量从弱到中等的怪物)",
    
    "Numerous Ancient Graveyards and undead lairs litter the landscape in this expansive region.[newline](Many weak to strong monsters present)[newline](Use of this pattern requires 128 meg RAM min) ":
        "在此广袤地区遍布着大量的远古墓地和不死生物巢穴。[newline](有大量从弱到强的怪物)[newline](使用此模式至少需要128兆内存) ",
    
    "A single Fortified Goblin Camp is protected by several Goblin Watchtowers.[newline](A few weak monsters are present)":
        "一座加固的哥布林营地由数座哥布林瞭望塔保护。[newline](有少量弱小的怪物)",
    
    "A large number of Trolls inhabit the nearby wilderness. Several goblin tribes have allied with the Trolls.[newline](Many average to strong monsters present)":
        "大量巨魔栖息在附近的荒野中。数个哥布林部落已与巨魔结盟。[newline](有大量从中等到强力的怪物)",
    
    "Greater Gorgons patrol this region. There is evidence that they are guarding at least a pair of spawning grounds.[newline](A few strong monsters are present)":
        "更强的戈贡在此地区巡逻。有证据表明它们至少守护着一对繁殖地。[newline](有少量强力的怪物)",
    
    "Undead in this region will rise up to assault your town.[newline](Many weak to strong monsters present)":
        "此地区的不死生物将崛起并袭击你的城镇。[newline](有大量从弱到强的怪物)",
    
    "Shamans from a warren of Ratmen and a tribe of Goblins have begun meeting in a secret castle in this region.[newline](Many weak to strong monsters present) ":
        "来自鼠人巢穴的萨满和一个哥布林部落开始在此地区的一座秘密城堡中会面。[newline](有大量从弱到强的怪物) ",
    
    "The undead walk the land under  the command of several Vampires.[newline](Many weak to strong monsters present)":
        "亡灵在数个吸血鬼的命令下游荡于这片土地。[newline](有大量从弱到强的怪物)",
    
    "Deserters from Srcylia's Serpent Legions have taken refuge in nearby ruins. Rumors report Harpies, Medusae, and even Dragon in the area.[newline](Some average to very strong monsters present) ":
        "来自赛西利亚蛇人军团的逃兵已在附近的废墟中避难。传闻该地区有鹰身女妖、美杜莎，甚至还有巨龙。[newline](有一些从中等到极强的高度怪物) ",
    
    "Dragons have laired in the region. Trolls wander the area near the lair.[newline](Some strong to very strong monsters present)  ":
        "巨龙在此地区筑巢。巨魔在巢穴附近徘徊。[newline](有一些从强到极强的怪物)  ",
    
    "The region was once home to a powerful sorcerer. Now only his minions and monstrous experiments remain.[newline](Many weak to very strong monsters)":
        "此地区曾是一位强大巫师的家园。如今只剩下他的仆从和可怕的实验产物。[newline](有大量从弱到极强高度怪物)",
    
    "A sizeable force of aggressive serpents have established several lairs in this region.[newline](Many strong to very strong monsters present)":
        "一支规模可观的凶猛蛇人队伍在此地区建立了数个巢穴。[newline](有大量从强到极强高度怪物)",
    
    "Yetis and Wendigo stalk this frozen region.[newline](A few very strong monsters present)":
        "雪人和温迪戈出没于这片冰冻地区。[newline](有少量极强高度怪物)",
    
    "Goblins have heavily fortified their camps in this region.[newline](Many weak to strong monsters)":
        "哥布林在此地区的营地设下了重重防御。[newline](有大量从弱到强高度怪物)",
    
    "Several Dragon lairs are present in the region.[newline](Few strong to very strong monsters present)":
        "此地区存在数个巨龙巢穴。[newline](有少量从强到极强高度怪物)",
    
    # ---- 建筑初始描述 ----
    "Settlement starts with a level one Palace,  a Temple to Agrela, a Temple to Dauros, a Temple to Fervus and a temple to Krypta. ":
        "定居点以一级王宫开局，包含一座阿格雷拉神殿、一座道罗斯神殿、一座费尔弗斯神殿和一座墓穴神殿。",
    
    "Settlement starts with a level one Palace, a Royal Gardens, a Guardhouse, and a level two Temple to Agrela. ":
        "定居点以一级王宫开局，包含一座皇家花园、一座守卫塔和一座二级阿格雷拉神殿。",
    
    "Settlement starts with a level one Palace, a Hall of Champions, a Warriors Guild and a level two Guard House.":
        "定居点以一级王宫开局，包含一座英杰殿、一座战士公会和一座二级守卫塔。",
    
    # ---- 难度描述 ----
    "Difficulty: Beginner": "难度：初级",
    "Difficulty: Advanced": "难度：高级",
    "Difficulty: Advanced  Requires: The Bell, the Book, and the Candle  The Forsaken Land  Rescue the Prince  ":
        "难度：高级  要求：钟、书与蜡烛  遗弃之地  营救王子  ",
    "Difficulty: Expert": "难度：专家级",
    "Difficulty: Expert  Requires: All Southern Quests": "难度：专家级  要求：所有南方任务",
    "Difficulty: Expert  Requires: Quest for the Crown  Quest for the Holy Chalice  The Wizard's Curse  ":
        "难度：专家级  要求：寻找王冠  寻找圣餐杯  巫师的诅咒  ",
    "Difficulty: Master  Requires: The Clash of Empires  Darkness Falls  The Fortress of Ixmil  Legendary Heroes  Rise of the Ratmen  Scions of Chaos  The Siege  Trade Routes  Urban Renewal  The Valley of the Serpents  ":
        "难度：大师  要求：帝国冲突  黑暗降临  伊克席尔要塞  传奇英雄  鼠人崛起  混沌之子  围攻  贸易路线  城市更新  毒蛇谷地  ",
    
    # ---- 任务简报长文本 ----
    "\x22Majesty, do you recall the legend of the evil wights, Styx and Stones, who terrorized the Northern Reaches? Well, the locals inform us that the crypts of these shades are nearby. Indeed, their evil influence can be felt seeping into the land, causing the dead to rest uneasily.[newline]As long as Styx and Stones dwell in this region, our expansion is threatened. You must find these ancient horrors, wrest them from their slumber, and dispatch them, once and for all. Unfortunately, the two are as inextricably linked in death as they were in life. It is said that while one lives, the other cannot truly die.\x22":
        "\x22陛下，您可记得邪恶幽灵斯提克斯和斯通斯的传说？他们曾让北境恐怖不安。当地人告诉我们，这些幽灵的墓穴就在附近。确实，他们的邪恶影响已经渗透到这片土地中，使亡者不得安息。[newline]只要斯提克斯和斯通斯还在此地区，我们的扩张就受到威胁。您必须找到这些古老的恐怖存在，将他们从沉睡中唤醒并一劳永逸地消灭他们。不幸的是，两人在死后如同生前一样紧密相连。据说只要一个还活着，另一个就无法真正死去。\x22",
    
    "\x22(...sigh...) How can I possibly tell our Sovereign the news?...[newline]Um, Majesty, I have good news and bad. The good news is your uncle died...[newline][newline]No, that won't do...[newline][newline](ahem) My Leige, your late uncle has bequeathed you a town!...of sorts...[newline][newline]Hmm, maybe more positive...[newline][newline]It pleases me to inform you that you've been granted a tremendous opportunity! You can be the first Ardanian sovereign to give the word \x22slum\x22 a good name!...[newline][newline]Oh, that won't work either. (...sigh...) it's no use...[newline][newline]Majesty, look out the window. Rogues, Elves, and debauchery as far as the eye can see. It's all yours, no thanks to your dead uncle. Think you can clean up that mess?\x22":
        "\x22（……叹气……）我该怎么跟陛下说这个消息呢？……[newline]嗯，陛下，我有一个好消息和一个坏消息。好消息是您的叔叔去世了……[newline][newline]不，这样说不行……[newline][newline]（咳咳）陛下，您已故的叔叔遗赠给您一座城镇！……算是吧……[newline][newline]嗯，也许应该更积极一点……[newline][newline]我很荣幸地通知您，您获得了一个绝佳的机会！您可以成为第一个让\x22贫民窟\x22这个词变得好听的阿丹尼亚君主！……[newline][newline]哦，这样说也不行。（……叹气……）没用了……[newline][newline]陛下，看看窗外吧。到处都是女盗贼、精灵和纵酒作乐。这些都是您的了——多亏了您死去的叔叔。您觉得您能收拾好这个烂摊子吗？\x22",
    
    "\x22Majesty. I'm here with the latest report from the Chief Tax Collector. He reports that the fiscal quarter will soon be ending and the treasury reports a deficit of...\x22[newline][newline]{Voice of the Liche Queen} [newline]\x22You dare interfere with the plans of the Liche Queen?  Your meddling presence has been like a thorn in my side. I will soon remove you and your sorry heroes, permanently.\x22[newline][newline] \x22...though such outlays could damage income potential if not properly balanced against spending in other areas.[newline][newline]Uh, Your Highness? Did you hear what I was saying?\x22":
        "\x22陛下。我带来了首席税务官的最新报告。他报告说本财政季度即将结束，国库报告亏损……\x22[newline][newline]{巫妖女王的声音} [newline]\x22你竟敢干涉巫妖女王的计划？你那碍事的行径就像扎在我肉中的刺。我很快就会把你和你那些可怜的英雄们永远除掉。\x22[newline][newline] \x22……不过这种支出如果未能与其他领域的开支取得适当平衡，可能会损害收入潜力。[newline][newline]呃，陛下？您听到我说的话了吗？\x22",
    
    "\x22My Liege, a most disturbing development. It seems that Ixmil, a mad arch-mage, has cast a blanket of fear over the Northern Reaches. Ixmil controls an imposing, mystic fortress that moves under its own power, appearing and disappearing from place to place. Each time the fortress appears, it disgorges a great host of hostile creatures.[newline]Ixmil has now turned his eye upon your newly acquired holdings. We must find some way to halt his reign of terror before your subjects become demoralized by his elusive raids. Otherwise we may be forced to retreat from these hard-fought lands.\x22":
        "\x22陛下，一个最令人不安的消息。似乎是伊克席尔，一个疯狂的传奇法师，在北境笼罩了一层恐惧之幕。伊克席尔控制着一座令人敬畏的神秘堡垒，它能自行移动，在各处出现又消失。每次堡垒出现时，都会吐出大批敌对生物。[newline]伊克席尔现在已经盯上了您新获得的领地。我们必须想办法阻止他的恐怖统治，否则您的臣民会因为他的偷袭而士气低落。否则我们可能被迫从这些来之不易的土地上撤退。\x22",
    
    "\x22Somehow, the Shrine of Light has been despoiled, causing its opposing shrine to grow greatly in power. The dreaded Black Phantoms have been drawn to the power of the Shrine of Darkness, and so long as they are permitted to feed upon it, they cannot be destroyed.[newline]It is said that an ancient sage was entombed in this place long ago - if we can find his tomb and restore him to life, perhaps he will know how to restore the Shrine of Light.[newline]Once the balance between the two shrines has been restored, it should be possible to destroy the Black Phantoms and eliminate their presence here for good.\x22":
        "\x22不知为何，光之神殿被亵渎了，导致其对立的神殿力量大增。可怕的黑色幽灵被黑暗神殿的力量所吸引，只要它们被允许吸取其力量，就无法被消灭。[newline]据说很久以前一位古代圣贤被埋葬在此地——如果我们能找到他的墓穴并让他复活，也许他会知道如何恢复光之神殿。[newline]一旦两座神殿之间的平衡恢复，就应该能消灭黑色幽灵并永远清除它们在此地的存在。\x22",
    
    "\x22Majesty, your populace is ill at ease. We rest on the edge of Krolm's Anvil, an ancient battlefield. Since before recorded time this place has lain desolate. It is said to be haunted by the spirits of those who died here, and many fell creatures lurk in its shadowed barrows and hills. Rangers consider it one of the most dangerous regions in all of Ardania.[newline]It is said that many ancient and powerful artifacts lay scattered where great heroes perished. Regardless of the danger, the recovery of these items would demonstrate your resourcefulness to the inhabitants of these cold Northern Reaches.\x22":
        "\x22陛下，您的民众惶恐不安。我们驻扎在克罗尔玛之砧的边缘，一处古老的战场。自有记载以来，此地一直荒凉。据说那些在此死去之人的灵魂在此出没，许多凶残的生物潜伏在阴暗的墓冢和丘陵中。游侠们认为这是整个阿丹尼亚最危险的地区之一。[newline]据说许多古老而强大的神器散落在伟大英雄陨落之处。无论危险如何，找回这些物品将向这些寒冷北境的居民展示您的智慧与勇气。\x22",
}

# ==================== 4. 合并所有翻译 ====================
dict1 = {}

# miss.log 简单条目
for en, cn in miss_translations.items():
    dict1[en] = cn

# rollback.log 完整翻译
for en, cn in rollback_translations.items():
    dict1[en] = cn

print(f"\ndict_1.json: {len(dict1)} entries")

# ==================== 5. 写出 dict_1.json ====================
dict1_path = os.path.join(SCRIPTS, "dict_1.json")
with open(dict1_path, 'w', encoding='utf-8') as f:
    json.dump(dict1, f, ensure_ascii=False, indent=2, sort_keys=True)

print(f"Written: {dict1_path}")

# ==================== 6. 清理 rollback.json ====================
# 删除导致乱码的有害短词条：这些词会匹配到其他单词的子串
# 规则：如果某个 rollback key 是另一个单词的子串（会破坏完整单词），就应该删除
# 具体需要删除的：
harmful_keys = [
    "Ant",        # 匹配 inhabitAnts, GiAnt, PhAntoms
    "Bee",        # 匹配 been
    "Dead",       # 匹配 undead, dead uncle
    "Dragon",     # 匹配 Dragons (但 Dragons 也需要翻译，改为加 s)
    "Fear",       # 可能匹配 fearful
    "Force",      # 匹配 forced, forceful
    "House",      # 匹配 Guard House → Guard 住房
    "Name",       # 匹配 surname
    "Rogues",     # 可能匹配其他
    "Thorn",      # 匹配 thorns
    "Walk",       # 匹配 walkway
    "Ice",        # 匹配 Voice
    "Art",        # 匹配 artifacts, start
    "Look",       # 匹配 outlook
    "Luck",       # 匹配 unlucky
    "Stag",       # 匹配 stage
    "Sun",        # 匹配 sundry
    "War",        # 匹配 Wizard War → 已在dict_1精确翻译
    "Dead",       # 重复
    "Dread",      # 匹配 dreaded
    "Bold",       # 可能匹配其他
    "Large",      # 匹配 largely
    "Small",      # 匹配 smaller
    "Demon",      # 匹配 demonic
    "Cloud",      # 匹配 clouded
    "Rock",       # 匹配 rocks
    "Storm",      # 匹配 stormy
    "Fire",       # 匹配 fire, fires
    "Gold",       # 匹配 golden
    "Green",      # 匹配 greene
    "Grave",      # 匹配 graveyard
    "Park",       # 匹配 parked
    "Rat",        # 匹配 Ratman (但 Ratman 需保留)
    "Ratman",     # 保留 - 完整单词
    "Bear",       # 匹配 Hellbear
    "Bone",       # 匹配 bones
    "Dark",       # 匹配 darkness
    "Light",      # 匹配 Lightning
    "Cold",       # 匹配 coldtouch
    "Gem",        # 匹配 gemstone
    "Giant",      # 匹配 Giant Spider
    "Goblin",     # 这个保留 - 它是独立的
    "Goblins",    # 保留
    "Palace",     # 匹配 Palaces
    "Temple to",  # 保留 - 不会匹配其他单词
    "Royal Gardens", # 保留
    "Guardhouse", # 保留
    "Hall of Champions", # 保留
    "Warriors Guild", # 保留
    "Lair",       # 匹配 lairs
    "Camp",       # 匹配 campaign
    "Tower",      # 匹配 towers
    "Towers",     # 保留
    "Fortress",   # 匹配 fortresses
    "Castle",     # 匹配 castles
    "Inn",        # 匹配 inner
    "Tomb",       # 匹配 tombs
    "Graveyard",  # 保留
    "Shrine",     # 匹配 shrines
    "Serpent",    # 匹配 serpents
    "Serpents",   # 保留
    "Spider",     # 匹配 spiders
    "Troll",      # 匹配 Trolls
    "Trolls",     # 保留
    "Varg",       # 保留
    "Vargs",      # 保留
    "Werewolf",   # 匹配 Werewolves
    "Werewolves", # 保留
    "Wendigo",    # 保留
    "Yeti",       # 匹配 Yetis
    "Yetis",      # 保留
    "Skeleton",   # 匹配 Skeletons
    "Skeletons",  # 保留
    "Vampire",    # 匹配 Vampires
    "Vampires",   # 保留
    "Medusa",     # 保留
    "Harpy",      # 匹配 Harpies
    "Harpies",    # 保留
    "Zombie",     # 匹配 Zombies
    "Gorgon",     # 匹配 Gorgons
    "Minotaur",   # 匹配 Minotaurs
    "Daemon",     # 匹配 Daemonwood
    "Daemonwood", # 保留
    "Elf",        # 匹配 Elves
    "Elves",      # 保留
    "Dwarf",      # 匹配 Dwarves
    "Dwarves",    # 保留
    "Gnome",      # 匹配 Gnomes
]

# 实际需要从 rollback.json 删除的：
# 原则：只删除那些会导致错误子串匹配的短词
# 保留那些不会误匹配的完整词
# 从 rollback.log 乱码分析，真正有害的是：
# Ant, Bee, Dead, Force, House, Name, Thorn, Walk, Ice, Art, Look, Luck, Stag, Sun
# Dread, Bold, Large, Small, Demon, Cloud, Rock, Storm, Fire, Gold, Green, Grave
# Park, Rat, Bear, Bone, Dark, Light, Cold, Gem, Giant, Lair, Camp, Tower, Fortress
# Castle, Inn, Tomb, Shrine, Serpent, Spider, Troll, Werewolf, Yeti, Skeleton, Vampire
# Harpy, Zombie, Gorgon, Minotaur, Daemon, Elf, Dwarf, Gnome, War, Dead
# 这些全是短的英语单词，容易匹配到其他单词内部

# 更安全的做法：删除所有"单词级"的 rollback 条目（2-8字母的纯字母词），
# 保留长词组（如 "Level 2", "Magic Bazaar", "Temple to" 等）
# 以及已验证不会误匹配的复数形式

harmful_set = set()
for key in harmful_keys:
    harmful_set.add(key)

# 读取现有 rollback.json
rb_path = os.path.join(SCRIPTS, "rollback.json")
with open(rb_path, 'r', encoding='utf-8') as f:
    rb = json.load(f)

print(f"\nOriginal rollback.json: {len(rb)} entries")

# 过滤
rb_clean = {}
removed = []
for k, v in rb.items():
    if k in harmful_set:
        removed.append(k)
    else:
        rb_clean[k] = v

print(f"Removed {len(removed)} harmful entries: {removed[:20]}...")
print(f"Cleaned rollback.json: {len(rb_clean)} entries")

# 写回 cleaned rollback.json
with open(rb_path, 'w', encoding='utf-8') as f:
    json.dump(rb_clean, f, ensure_ascii=False, indent=2, sort_keys=True)
print(f"Updated: {rb_path}")

# ==================== 7. 验证 dict_1.json 中没有重复 dict.json 的 key ====================
dict_path = os.path.join(SCRIPTS, "dict.json")
with open(dict_path, 'r', encoding='utf-8') as f:
    main_dict = json.load(f)

dupes = [k for k in dict1 if k in main_dict]
if dupes:
    print(f"\nWARNING: {len(dupes)} keys already in dict.json: {dupes[:5]}")
    # 删除重复的
    for k in dupes:
        del dict1[k]
    with open(dict1_path, 'w', encoding='utf-8') as f:
        json.dump(dict1, f, ensure_ascii=False, indent=2, sort_keys=True)
    print(f"Removed duplicates, dict_1.json now has {len(dict1)} entries")
else:
    print("\nNo duplicates with dict.json")

print("\nDone!")
