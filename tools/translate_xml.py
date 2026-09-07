#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
M1 HD XML 文件翻译脚本

处理:
1. InterfaceStrings.xml (9条)
2. Quests/*.mqxml (22个任务文件, Name+Short+Long)
3. Quests/*_Text.xml (2个, 胜负消息)
4. QuestsMX/*.mqxml (14个资料片任务文件)
5. Freestyle.mqxml / mx_Freestyle.mqxml (自由模式, 仅Name)

输出:
   output/Data/InterfaceStrings.xml
   output/Quests/*.mqxml
   output/Quests/*_Text.xml
   output/QuestsMX/*.mqxml
"""
import os
import sys
import xml.etree.ElementTree as ET
import json

# ============================================================
# 路径
# ============================================================
WS = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace'
HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'

# ============================================================
# 术语表 (从 glossary.py 继承核心术语)
# ============================================================
# 建筑名
GUILD_NAMES = {
    "Warriors Guild": "战士公会",
    "Rangers Guild": "游侠公会",
    "Wizards Guild": "法师公会",
    "Rogues Guild": "盗贼公会",
    "Temple to Helia": "赫利亚神殿",
    "Temple to Krypta": "克里普塔神殿",
    "Temple to Agrela": "阿格蕾拉神殿",
    "Temple to Fervus": "弗瓦斯神殿",
    "Temple to Krolm": "克罗尔玛神殿",
    "Temple to Dauros": "达罗斯神殿",
    "Elven Lounge": "精灵休闲屋",
    "Elven Bungalow": "精灵别馆",
    "Dwarven Settlement": "矮人据点",
    "Blacksmith": "铁匠铺",
    "Marketplace": "集市",
    "Fairgrounds": "竞技场",
    "Sorcerers Abode": "术士居所",
    "Magic Bazaar": "魔法集市",
    "Gambling Hall": "赌场",
    "Guardhouse": "警卫室",
    "Palace": "宫殿",
    "Outpost": "前哨",
    "Embassy": "使馆",
    "Trading Post": "贸易站",
    "Inn": "酒馆",
    "Graveyard": "墓地",
    "Sewer": "下水道",
    "Broken Sewer Main": "破损下水主管",
    "General Housing": "平民住宅",
}

# 英雄/单位名
HERO_NAMES = {
    "Warrior": "战士",
    "Ranger": "游侠",
    "Wizard": "法师",
    "Rogue": "盗贼",
    "Paladin": "圣骑士",
    "Solarii": "索拉莉",
    "Cultist": "狂信徒",
    "Warrior of Discord": "纷争战士",
    "Priestess": "女祭司",
    "Dwarf": "矮人",
    "Elf": "精灵",
    "Goblin": "哥布林",
    "Ratmen": "鼠人",
    "Peasant": "农民",
    "Guard": "卫兵",
}

# 怪物名
MONSTER_NAMES = {
    "Vendral": "凡德拉尔",
    "Styx": "斯提克斯",
    "Stones": "斯通斯",
    "Url-Shekk": "乌尔-谢克",
    "Wight": "尸鬼",
    "Wights": "尸鬼",
    "Black Phantom": "黑色幻影",
    "Black Phantoms": "黑色幻影",
    "Ice Dragon": "冰霜巨龙",
    "Yeti": "雪人",
    "Rat King": "鼠王",
    "Rhoden": "罗登",
    "Abomination": "憎恶",
    "Dragon King": "龙王",
    "Liche Queen": "巫妖女王",
    "Witch King": "巫王",
    "Demon": "恶魔",
    "Vampire": "吸血鬼",
    "Dragon": "巨龙",
    "Shadow Beast": "暗影兽",
    "Medusa": "美杜莎",
    "Undead": "亡灵",
}

# 地名/人名
PLACE_NAMES = {
    "Ardania": "阿达尼亚",
    "Northern Reaches": "北境",
    "Mountains of Doom": "厄运山脉",
    "Valmorgen": "瓦尔莫根",
    "Krolm's Anvil": "克罗尔玛之砧",
    "Shrine of Light": "光明圣坛",
    "Shrine of Darkness": "黑暗圣坛",
    "Crown of Sydrian": "西德里安之冠",
    "Brashnard": "布拉什纳德",
    "Ixmil": "伊克斯米尔",
    "Borjin": "博尔金",
    "Juleck": "朱莱克",
    "Tarsival": "塔西瓦尔",
    "Rezzenthell": "雷岑瑟尔",
    "Glohrea Oathtaker": "格洛蕾亚·誓约者",
    "Byran the Dragonsmiter": "屠龙者拜兰",
    "Andrevus": "安德烈弗斯",
    "Shallevex": "沙列维克斯",
}

# 神名
GOD_NAMES = {
    "Krolm": "克罗尔玛",
    "Helia": "赫利亚",
    "Krypta": "克里普塔",
    "Agrela": "阿格蕾拉",
    "Fervus": "弗瓦斯",
    "Dauros": "达罗斯",
}

# 任务名
QUEST_NAMES = {
    "BARREN_WASTE": "荒芜之地",
    "BELL_BOOK_CANDLE": "钟书烛",
    "BRASHNARD": "布拉什纳德",
    "DARK_FOREST": "暗影森林",
    "DAY_OF_RECKONING": "清算之日",
    "DEAL_DEMON": "恶魔交易",
    "ELVEN_TREACHERY": "精灵的背叛",
    "FERTILE_PLAIN": "丰饶平原",
    "FORSAKEN_LANDS": "被遗弃之地",
    "SLAVE_PITS": "奴隶坑",
    "GOBLIN_HORDES": "哥布林大军",
    "HOLY_CHALICE": "圣杯",
    "LICHE_QUEEN": "巫妖女王",
    "MAGIC_RING": "魔法戒指",
    "QUEST_FOR_CROWN": "皇冠之争",
    "SAVE_PRINCE": "拯救王子",
    "SLAY_DRAGON": "屠龙",
    "TOMB_DRAGON": "龙之墓",
    "VAMPIRIC_REVENGE": "吸血鬼的复仇",
    "WIZARDS_CURSE": "巫师的诅咒",
    "WRATH_OF_KROLM": "克罗尔玛之怒",
    "MD01": "克罗尔玛之怒",
    # MX
    "XQD1": "暮光之衡",
    "CLASH_EMPIRES": "帝国冲突",
    "DARKNESS_FALLS": "黑暗降临",
    "FORTRESS_IXMIL": "伊克斯米尔要塞",
    "LEGENDARY_HEROES": "传奇英雄",
    "RISE_RATMEN": "鼠人崛起",
    "SCIONS_CHAOS": "混沌之子",
    "SIEGE": "围城",
    "SPIRES_DEATH": "死亡之塔",
    "TRADE_ROUTES": "贸易路线",
    "URBAN_RENEWAL": "城市重建",
    "VALE_SERPENTS": "毒蛇之谷",
    "VIGIL": "守夜",
    "FREESTYLE": "自由模式",
    "MX FREESTYLE": "扩展自由模式",
}

# ============================================================
# 翻译表：InterfaceStrings.xml
# ============================================================
INTERFACE_STRINGS = {
    "Quest Data Missing": "任务数据缺失",
    "The Quest data used to create this save is missing.  The Quest may need to be re-downloaded and installed in your user folder.": "用于创建此存档的任务数据缺失。该任务可能需要重新下载并安装到您的用户文件夹中。",
    "ACTIVATE MODS": "激活模组",
    "DEACTIVATE MODS": "停用模组",
    "Works with: Original Majesty Quests": "适用：原版任务",
    "Works with: Northern Expansion Quests": "适用：北境扩展任务",
    "Works with: Original and Northern Expansion Quests": "适用：原版及北境扩展任务",
    "Works with: Unknown Data Set": "适用：未知数据集",
    "Mods Missing": "模组缺失",
}

# ============================================================
# 翻译表：Text.xml 胜负消息
# ============================================================
TEXT_XML = {
    "You have defeated the vampires!": "你击败了吸血鬼！",
    "You have lost to the vampires!": "你被吸血鬼击败了！",
    '"Well done, Your Majesty. With the defeat of Krolm\'s Avatar, his influence over these lands has been broken.[newline][newline]Most of Krolm\'s followers have scattered back into the wilderness and these routes into the north are now once again safe for civilized trade."': '"干得漂亮，陛下。随着克罗尔玛化身被击败，他对这片土地的控制已被打破。[newline][newline]克罗尔玛的大多数追随者已四散逃回荒野，这些通往北方的路线如今再次恢复了安全的文明贸易。"',
    "Krolm's power has proven to be too great for you and his will remains supreme in these lands - for now.": "克罗尔玛的力量对你而言过于强大，他的意志在这些土地上依然至高无上——暂时如此。",
}

# ============================================================
# 翻译表：任务 Short 描述
# ============================================================
QUEST_SHORT = {
    "BARREN_WASTE": "建造一座竞技场即可完成任务。[newline][newline]（无人族以外的种族或法师可用。）",

    "BELL_BOOK_CANDLE": "寻回从宫殿中被盗并藏匿于各巢穴中的三件物品。[newline][newline]（本任务中无战士或法师可用。）",

    "BRASHNARD": "寻回组成布拉什纳德终极之球的七块碎片。",

    "DARK_FOREST": "治愈冒险者们的疾病。[newline][newline]击败盘踞在塔中的巫王。",

    "DAY_OF_RECKONING": "在王国中最邪恶生物的猛攻下存活下来。[newline][newline]（不可建造任何神殿或非人族居所。）",

    "DEAL_DEMON": "在40天内赚取100,000金币，否则任务失败。",

    "ELVEN_TREACHERY": "在30天内赚取50,000金币支付赎金，[newline][newline]或[newline][newline]在此之前击败精灵及所有敌方英雄和建筑。[newline][newline]（本任务中无精灵加入你的城市。）",

    "FERTILE_PLAIN": "在七波掠夺者的猛攻下存活下来。[newline][newline]消灭所有掠夺者！[newline][newline]（本任务中无人族以外的种族可用。）",

    "FORSAKEN_LANDS": "摧毁邪恶城堡即可完成此史诗任务。[newline][newline]（无法师公会、非人族种族、三级宫殿或任何神殿可用。）",

    "SLAVE_PITS": "摧毁四座奴隶坑，释放所有奴隶。[newline][newline]杀死三头野兽乌尔-谢克。",

    "GOBLIN_HORDES": "摧毁所有哥布林据点。[newline][newline]（不可建造游侠公会。）",

    "HOLY_CHALICE": "在三十天内寻回圣杯，否则疾病将吞噬你。[newline][newline]有传言称乡下某处有一群强大的圣骑士。若能找到他们，或许能助你一臂之力……[newline][newline]当心！此地的巢穴不会消亡——只会暂时沉寂。[newline][newline]（本任务中不可建造集市。）",

    "LICHE_QUEEN": "将巫妖女王从她的岩穴巢穴中逐出，然后击败她。[newline][newline]（无克里普塔神殿可用。不可建造铁匠铺、酒馆、矮人据点或盗贼公会，但可能有早期定居点的遗迹残留……）",

    "MAGIC_RING": "寻回治愈之戒。",

    "QUEST_FOR_CROWN": "找到并摧毁藏有王冠的巢穴。",

    "SAVE_PRINCE": "摧毁囚禁王子的塔楼监狱即可完成此史诗任务。利用法师公会的魔法援助你。[newline][newline]（无神殿、非人族种族或游侠公会可用。）",

    "SLAY_DRAGON": "你唯一拥有的公会是一座赫利亚神殿。[newline][newline]你无法建造任何公会或神殿，因此必须找到并组织当地人以保卫你的据点。使用索拉莉来召集乡间的公会！[newline][newline]找到隐藏之剑的地点并摧毁之，以获得能使凡德拉尔变为凡躯的魔法之剑。[newline][newline]用魔法之剑斩杀凡德拉尔。[newline][newline]（本任务中不可建造任何公会或神殿。）",

    "TOMB_DRAGON": "摧毁三个怪物巢穴以揭示陵墓的位置。[newline][newline]摧毁龙王之墓。[newline][newline]（在这片险恶之地没有卫兵愿意外出，因此你无法建造警卫室。）",

    "VAMPIRIC_REVENGE": "- 建立一个小型王国并招募英雄，以在此地区站稳脚跟。[newline]- 摧毁黑暗城堡，消灭其中邪恶的居民……",

    "WIZARDS_CURSE": '解除诅咒有两种方法：[newline][newline]方法一：抓住偷走法术书的流氓，将其归还给法师，然后完成法师认为"足够的善行"。[newline][newline]方法二：消灭愤怒的法师。[newline][newline]你的英雄们被诅咒了，他们不会像往常那样行动……[newline][newline]（本任务中无法师公会和弗瓦斯、克里普塔、阿格蕾拉、克罗尔玛或达罗斯神殿可用。）',

    "MD01": "摧毁所有克罗尔玛祭坛，或击败他的化身，以从克罗尔玛的掌控中夺取这片土地。[newline]此地的野生生物已被克罗尔玛之灵强化。[newline]你只能建造公会、神殿、塔楼和铁匠铺。[newline]你不可建造克罗尔玛神殿。",

    # ===== QuestMX =====
    "XQD1": "保卫你的王国免受黑色幻影的侵袭。[newline]找到古代智者的安息之所并使他复生。[newline]重新圣化光明圣坛。[newline]消灭黑色幻影。",

    "CLASH_EMPIRES": "消灭所有鼠人和哥布林巢穴。",

    "DARKNESS_FALLS": "消灭邪恶的尸鬼。[newline][newline]（矮人不会迁入此地区，因为他们对此心怀畏惧。）",

    "FORTRESS_IXMIL": "找到并摧毁伊克斯米尔的要塞。",

    "LEGENDARY_HEROES": "在三十天内摧毁所有古冢，否则此地区不稳定的能量将把你的王国化为废墟。[newline][newline]每座建筑同一时间只能容纳一名英雄。[newline][newline]英雄初始经验很高。[newline][newline]升级二三级宫殿所需英雄更少。[newline][newline]此地区部分巢穴会在一段时间后重生。",

    "RISE_RATMEN": "消灭鼠王罗登及王国中所有破损下水主管。",

    "SCIONS_CHAOS": "消灭混沌三子——女祭司、狂信徒和纷争战士。[newline][newline]（此地仅可建造克里普塔和弗瓦斯神殿。也无法师公会、集市、非人族种族、前哨或使馆可建。）",

    "SIEGE": "摧毁博尔金的宫殿或切断为其供金的商队路线。如果他们资源耗尽，必将被迫向我军投降。[newline][newline]（无法师公会可建。）",

    "SPIRES_DEATH": "找到并摧毁五座尖塔中的每一座。[newline][newline]当心！每摧毁一座，其余的便会愈发强大……",

    "TRADE_ROUTES": "清理路线并保护下个月进入我王国的商队。至少一半商队需完好抵达集市。若集市被毁，则任务失败。[newline][newline]（不可建造集市或贸易站。）",

    "URBAN_RENEWAL": "摧毁所有红色公会、精灵休闲屋和赌场。[newline][newline]（不可建造盗贼公会或精灵别馆。）",

    "VALE_SERPENTS": "摧毁不断蚕食你领地的蛇坑。[newline][newline]若失去所有精灵别馆，则任务失败。[newline][newline]你不可主动放置精灵别馆，由你的精灵邻居决定何时何地建造……[newline][newline]许多平民拒绝在此险谷定居。因此，平民住宅、下水道、破损下水主管和墓地等常规基础设施不会出现。",

    "VIGIL": "消灭藏匿于山丘中的憎恶。[newline][newline]（不可建造竞技场、术士居所、魔法集市、前哨、公会或神殿。）",

    "FREESTYLE": "自由模式游戏参数",
    "MX FREESTYLE": "扩展自由模式游戏参数",
}

# ============================================================
# 翻译表：任务 Long 描述
# ============================================================
QUEST_LONG = {
    "BARREN_WASTE": '"不必灰心，陛下。尽管这片沼泽地区尚未改造成舒适的居所，但我相信我们能实现您母亲的梦想。我建议我们从小处着手，想办法更有效地利用这片沼泽地。也许在这片荒凉的泥沼中央建造一座热闹的竞技场，就足以证明您的成就与能力。"',

    "BELL_BOOK_CANDLE": '"恕我冒昧，殿下，但我们遭贼了！有人偷走了魔法之钟、圣典和永恒之烛！就在我们眼皮底下，它们被某个恶棍从皇家宝库中卷走。我们必须想办法找回它们！那可是无价之宝。"',

    "BRASHNARD": '"我理解殿下是在寻找布拉什纳德的终极之力球？拥有那件传奇神器必定会为您的统治增添威权。[newline]您想必还记得，此球被分成七块碎片，散落到了王国的四面八方。必须召集大量英雄，在境内搜寻这些失落的碎片。以您的权威，让这项壮举开始吧。[newline]陛下？这不是卑微的仆人该质疑您英明指示的地方，但此事……嗯，"雄心勃勃"。我们真的能在无数前人失败之处成功吗？"',

    "DARK_FOREST": '"一股诅咒降临了这片土地，陛下，在消耗您忠诚子民的力量。几乎没有人有力气离开家门。[newline]当地公会仅剩一名战士尚能行动。他主动提出帮忙寻找治疗此疫的方法。[newline]我……我建议您接受他的好意。失陪了，我真的得躺下了。"',

    "DAY_OF_RECKONING": '"腐败从大地中萌发！冥界的军团正纷纷涌现，蹂躏这片土地。这场邪恶的瘟疫定是天界有恙之兆——诸神开战，我们全都身处险境！[newline]王国必须从这场席卷一切的混乱中得到保卫！尽管凡人之力看似脆弱的威慑，但您必须召唤一切可能的力量，与这些污秽与瘟疫的终极化身进行清算。"',

    "DEAL_DEMON": '"伟大的领袖，恕我冒昧，但有一个可怕的存在造访了我们的城堡！一个恶魔！他声称是来收取您母亲欠他的债务。这是真的吗？[newline]由于我们无法立即交付要求的赏金，他说稍后会来取三倍于此的金额，否则就诅咒整个村庄。说完他便在一团恶臭的绿色烟雾中消失了。[newline]陛下？希望您已有对策。在他回来之前必须做些什么！"',

    "ELVEN_TREACHERY": '"陛下？很遗憾由我来转达如此令人难以忍受的消息。一个精灵无赖刚离开了城堡。他来传达消息说，他那帮不靠谱的同族绑架了您的儿子！显然他们只看到了您金库的规模，而非您高超领导力的深度。[newline]这些卑劣的机会主义者要求您支付赎金，否则——他们说——您将再也见不到活着的儿子。我们该如何应对这些精灵的傲慢无礼，殿下？"',

    "FERTILE_PLAIN": '"能辅佐如此英明的君主是我的荣幸。您的先祖定会为您在这片土地上培育的繁荣感到自豪。得益于您近期的土地 acquisitions，此地区已变得富饶而宜居。那位男爵居然只用一百五十头牛就把它换走了，真是太妙了。[newline]然而，如此繁荣不会无人注意。已传到城墙边的消息说，许多人觊觎这片田园诗般的所在。您必须防备任何此类掠夺者。"',

    "FORSAKEN_LANDS": '"殿下，尽管这片土地前景广阔，但我必须指出有许多事需要您立即关注。我们的城镇仍受到潜伏在这片被遗弃之地的掠夺怪物的袭扰。[newline]只有当这些入侵者被挡在外面时，我们才能真正繁荣。善用各类英雄的服务符合您的利益。凭借他们的技能和诸神的庇佑，我们或许能够守护这个社区。"',

    "SLAVE_PITS": '"令人不安的传言已传入我耳中，陛下。许多报告称村镇边缘的村民失踪了。恐惧的窃窃私语说，某个邪恶的生物正在收割不知情的人，将其作为奴隶用于它的邪恶计划。[newline]恐慌正在村中蔓延。殿下，必须采取行动，否则我们将面临严重的危机。我建议您着手找回这些民众，并清除这片土地上的可怕威胁。"',

    "GOBLIN_HORDES": '"可怕的消息！前线的斥候报告说，哥布林正在大规模向这边开来。我们必须在他们淹没并彻底摧毁王国之前遏制这股黑暗浪潮。我建议您招募英雄——尽可能多地——来保护我们的村庄并抵挡这群卑劣的虫群。[newline]恳请您迅速行动！最近一次目击距村庄已不远，而我们对此等入侵毫无准备。"',

    "HOLY_CHALICE": '"陛下最近的病情已让百姓备受煎熬。他们对您受苦的哀痛只增不减，更有噩耗传来——您的病情并未好转。如今应已清楚，是黑暗法术在推动这阴险的疾病。[newline]若非有一些令人振奋的消息，我不会打扰您休养。可信的老法师雷岑瑟尔告诉我有一件圣物——一只圣杯——蕴含着不可思议的治愈之力。它就藏在此地的某处。我恳求您派出英雄搜寻这片土地以找到这只圣杯。这可能是终结您疾病的唯一方法。"',

    "LICHE_QUEEN": '"陛下。我带着首席税务官的最新报告来了。他报告说本季度即将结束，国库报告赤字……"[newline][newline]{巫妖女王的声音}[newline]"你竟敢干扰巫妖女王的计划？你那碍事的行径如芒在背。我很快就会永远除掉你和你那些可悲的英雄。"[newline][newline]"……此类支出若未与其它领域的开支妥善平衡，可能损害收入潜力。[newline][newline]呃，殿下？您听到我刚才说的话了吗？"',

    "MAGIC_RING": '"陛下？我感到视线变得越来越暗了。笼罩这片土地的黑暗瘟疫并不理会这些城堡墙壁的边界。[newline]这场流行病席卷我们的土地，榨取着皇家子民的生命。我们最好的医生似乎也无法减缓这可怕疾病的步伐。[newline]在陷入昏迷之前，强大的法师雷岑瑟尔坚持说只有一种方法可以阻止这场灾祸。您必须找回治愈之戒，传说中它就藏在阿达尼亚的某处。[newline]陛下，请以最快的速度行动……"',

    "QUEST_FOR_CROWN": '"令人振奋的发现，殿下！西德里安之冠——您家族失传已久的权力象征——已被找到。它被藏在前线的一座邪恶堡垒中。[newline]最近一波又一波的怪物袭击已让您的子民心灰意冷。收回此物无疑将提升子民们的信心并恢复对您家族的支持。我极力建议您抓住这个意外之机。"',

    "SAVE_PRINCE": '"今天早上，在清理皇家信鸽巢时，一名仆人注意到一只陌生的鸟。它的腿上绑着一条消息。内容如下：[newline]"我被困在一座邪恶法师的塔楼中！请、请、请救救我。我的母亲，瓦尔莫根的女王，会给你丰厚的报酬。"[newline]斥候确认瓦尔莫根的年轻王子近日在狩猎途中失踪。此次救援的报酬和善意将对我们的王国大有裨益。"',

    "SLAY_DRAGON": '"恕我冒昧，陛下，但您必须醒来！先知朱莱克预言的那一天已经到来。强大的巨龙凡德拉尔已从厄运山脉的千年沉睡中苏醒，正在攻击王国！[newline]时间不多了。您必须招募部队去寻找能帮助消灭这一威胁的武器，否则王国将化为焦土。派英雄去寻找屠龙者拜兰之剑。只有这种利刃能劈开凡德拉尔的龙皮。"',

    "TOMB_DRAGON": '"陛下，我带来了重要的消息。老法师塔西瓦尔通过他的水晶和灵能，占卜出了您王国的命运。他在此类事务上的预见通常相当准确，因此我恳请您重视他的预言。[newline]法师声称看到了一座古墓，已被所有人遗忘。它被重新发现并被许多英雄造访。其中一人最终取出了藏于其中的一把魔剑。塔西瓦尔坚持说为了这片土地的福祉，这件事必须发生。"',

    "VAMPIRIC_REVENGE": '"陛下！[newline]关于一个吸干生命的威胁的传言正在乡间如野火般蔓延。您必须领导一场英雄的远征，将这股邪恶势力从我们美丽的土地上驱逐出去！"',

    "WIZARDS_CURSE": ' "恕我如此无礼打扰，但有一位愤怒的法师……"[newline][newline]{愤怒的法师}[newline]"让开，你这个卑躬屈膝的癞蛤蟆！[newline]君主！我要和你谈谈。在你这无法无天的治下，你一个愚蠢的子民偷走了我的法术书。那本伟大的典籍价值不可估量！你和你的领地必须为它的损失付出代价！[newline]在做出足够的善行之前，我诅咒这片土地上的所有居民像死者一样愚笨。"',

    "MD01": '"克罗尔玛的追随者已质疑您统治这片蛮荒北境的资格，陛下。在我们面前是一座为供奉克罗尔玛而建的圣谷。据说在此地，克罗尔玛之灵本身会现身，消灭任何胆敢闯入其圣地的人。"[newline]"其他诸神不认可克罗尔玛对整个北境的独占主张，他们的祭司已交付我们任务——削弱他在此地的影响力。但要警惕，陛下——克罗尔玛在此地的力量毋庸置疑。空气中脉动着他的存在，他追随者的战吼在这些山丘间永不止息地回荡。"',

    # ===== QuestMX Long =====
    "XQD1": '"不知何故，光明圣坛被亵渎了，导致其对立的圣坛力量大增。可怕的黑色幻影被黑暗圣坛的力量所吸引，只要它们被允许汲取其中的力量，就无法被消灭。[newline]据说很久以前有一位古代智者被葬于此地——如果我们能找到他的墓穴并使他复生，或许他会知道如何恢复光明圣坛。[newline]一旦两座圣坛之间的平衡恢复，就应该能消灭黑色幻影，永久根除它们在此地的存在。"',

    "CLASH_EMPIRES": '"殿下，我很高兴地向您报告，我们最新的北方定居点繁荣昌盛。第一季的收成已入仓，集市蓬勃发展！[newline]啊……不过有一个小问题。似乎某些哥布林和鼠人派系一心想要消灭对方。虽然这通常值得庆祝，但他们选择的战场恰好正对着我们的新村庄。[newline]我担心，在他们急于消灭对方的冲动下，我们的定居点会被淹没。您的子民恳请您在他们被这些卑劣虫群的洪流冲走之前采取行动。"',

    "DARKNESS_FALLS": '"陛下，您可还记得邪恶尸鬼斯提克斯和斯通斯肆虐北境的传说？当地人告诉我们，这些幽影的墓穴就在附近。确实，它们的邪恶影响正渗入这片土地，使亡者不得安息。[newline]只要斯提克斯和斯通斯盘踞于此，我们的扩张就受到威胁。您必须找到这些古老的恐怖存在，将它们从沉睡中唤醒，然后一劳永逸地予以消灭。不幸的是，两人生死相系，如同生前一般密不可分。据说只要其中一个还活着，另一个就无法真正死去。"',

    "FORTRESS_IXMIL": '"殿下，传来极为令人不安的消息。似乎疯狂的大法师伊克斯米尔已向北境笼罩了一层恐惧。伊克斯米尔控制着一座令人畏惧的神秘要塞，它能自行移动，从一个地方出现又消失。每次要塞出现，都会吐出一大群充满敌意的生物。[newline]伊克斯米尔如今已将目光投向您新获得的领地。我们必须想办法阻止他的恐怖统治，否则您的子民将被其出没不定的袭击所挫伤。否则我们可能不得不从这些来之不易的土地上撤退。"',

    "LEGENDARY_HEROES": '"陛下，您的子民心中不安。我们驻扎在克罗尔玛之砧的边缘，这是一片古老的战场。自有记载之前，此地便已荒芜。据说那些在此死去之人的幽灵在此地出没，许多凶猛的生物潜伏在其阴暗的古冢和山丘中。游侠将其视为整个阿达尼亚最危险的地区之一。[newline]据说许多古老而强大的神器散落在那些伟大英雄陨落之处。无论危险如何，找回这些物品将向这些寒冷北境的居民展示您的胆识。"',

    "RISE_RATMEN": '"君主，鼠人日益猖獗。它们的数量似乎在不断增长，从我们的下水道中传来奇怪的新声响。有传言说鼠人中出现了一位新统治者——一只渴望征服地表的鼠王。[newline]我们必须警惕鼠人可能的入侵。如果它们选择发动攻击，我们必须瞄准它们的新首领。没有了统治者，鼠人将无法维持如此规模的攻势。"',

    "SCIONS_CHAOS": '"事情变得有趣了，殿下。三位被称为"混沌之子"的传奇英雄向我们定居点的勇士们发出了挑战。他们要求我们的英雄在战场上与他们会面。必须在真正击败全部三位混沌之子后，我们才能宣称已战胜他们。"',

    "SIEGE": '"陛下，我们与博尔金这个敌对而野蛮的王国之间的战斗，已使我们围攻其据点。我们必须通过封锁其补给商队来迫使其投降，或者强攻其堡垒以武力夺取。挑战的复杂性在于本地的法师公会选择站在博尔金一边。尽管如此，我觉得只要您果断行事，我们必能取胜。"',

    "SPIRES_DEATH": '"北境最富饶的土地就在我们眼前，触手可及。然而，通往那些土地的道路被几座巨大的尖塔所阻挡，它们向任何靠近者投掷魔法闪电并召唤凶猛的怪物。[newline]一位当地游牧民解释说，很久以前，一支北方民族极大地激怒了他们的神，以至于被驱逐出家园，被禁止回归。神随后竖立了五座受诅咒的尖塔来阻止他们。[newline]虽然这位神的名字早已被遗忘，但她的尖塔依然矗立。如果您能清除这些障碍，我们便可进入那片充满希望的土地。"',

    "TRADE_ROUTES": '"陛下，我刚从几个北方王国归来。它们对贸易持开放态度，并提供各种奇珍异货。然而，它们的信任需要争取。[newline]在接下来的几周里，一支支商队将开辟新的贸易路线，前往我们的定居点。如果我们能确保它们安全通行，这便履行了我们的契约，我们将从北方获得源源不断的货物和财富。"',

    "URBAN_RENEWAL": '"（……叹气……）我该如何向我们的君主报告这个消息？……[newline]嗯，陛下，我有好消息也有坏消息。好消息是您的叔叔去世了……[newline][newline]不，这不行……[newline][newline]（咳咳）殿下，您已故的叔叔遗赠给您一座城镇！……某种意义上……[newline][newline]嗯，也许说得更积极些……[newline][newline]我很高兴地通知您，您获得了一个千载难逢的机会！您可以成为第一位让"贫民窟"这个词变得好听的阿达尼亚君主！……[newline][newline]哦，这也不行。（……叹气……）没用的……[newline][newline]陛下，看看窗外吧。盗贼、精灵，还有放眼所及的纵欲狂欢。这一切都归您所有，都怪您那个死掉的叔叔。您觉得您能收拾好那堆烂摊子吗？"',

    "VALE_SERPENTS": '"殿下，我们最新的定居点与一个精灵聚落接壤。他们致以最热情的问候，并宣誓与我们结盟，以期改善贸易和关系。[newline]不太好的消息是，商人们之间有些风声。他们说在精灵领地的远方边界与一个强大对手发生了冲突。也许我们应该做好防备，以防这些不仅仅是空穴来风。"',

    "VIGIL": '"殿下，痛心地告诉您，我们王国最英勇、最著名的圣骑士格洛蕾亚·誓约者在外出冒险时阵亡了。这个村子里没有一个英雄或农民不欠她无私行为的救命之恩。您的子民要求 whoever...or whatever 谋杀了我们勇敢的格洛蕾亚的项上人头。请迅速行动，派遣一支精锐英雄小队前往荒野。此仇不能不报！"',

    "FREESTYLE": "",
    "MX FREESTYLE": "",
}

# ============================================================
# 应用术语替换
# ============================================================
def apply_terms(text):
    """在翻译文本中应用术语替换（统一用词）"""
    result = text
    # 不需要额外替换，翻译时已直接使用中文术语
    return result

# ============================================================
# 处理 XML 文件
# ============================================================
def process_interface_strings():
    """翻译 InterfaceStrings.xml
    
    之前因 UTF-8 文本渲染循环卡死而保留原始英文，
    现已通过 ASI DLL patch 修复渲染循环，可安全翻译。
    """
    print("\n=== Processing InterfaceStrings.xml ===")
    src = os.path.join(HD_GAME, 'backup_original', 'Data', 'InterfaceStrings.xml')
    if not os.path.exists(src):
        src = os.path.join(HD_GAME, 'Data', 'InterfaceStrings.xml')
    
    with open(src, 'r', encoding='utf-8') as f:
        content = f.read()
    
    for en, cn in INTERFACE_STRINGS.items():
        content = content.replace(en, cn)
    
    out_dir = os.path.join(WS, 'output', 'Data')
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, 'InterfaceStrings.xml')
    
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"  -> {out_path} (translated, {len(content)} bytes)")

def process_text_xml():
    """翻译 *_Text.xml 文件"""
    print("\n=== Processing Text.xml files ===")
    for fname in ['VampiricRevenge_Text.xml', 'WrathOfKrolm_Text.xml']:
        src = os.path.join(HD_GAME, 'Quests', fname)
        with open(src, 'r', encoding='utf-8') as f:
            content = f.read()
        
        for en, cn in TEXT_XML.items():
            content = content.replace(en, cn)
        
        out_dir = os.path.join(WS, 'output', 'Quests')
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, fname)
        with open(out_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"  -> {out_path}")

def process_mqxml(dirname):
    """翻译 mqxml 文件"""
    src_dir = os.path.join(HD_GAME, dirname)
    out_dir = os.path.join(WS, 'output', dirname)
    os.makedirs(out_dir, exist_ok=True)
    
    print(f"\n=== Processing {dirname}/*.mqxml ===")
    
    for fname in sorted(os.listdir(src_dir)):
        if not fname.endswith('.mqxml'):
            continue
        
        src = os.path.join(src_dir, fname)
        with open(src, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 找到 Name 标签翻译任务名
        for en_name, cn_name in QUEST_NAMES.items():
            content = content.replace(f'<Name>{en_name}</Name>', f'<Name>{cn_name}</Name>')
        
        # 翻译 Short 和 Long 描述
        # 使用XML解析器更精确地替换
        try:
            tree = ET.parse(src)
            root = tree.getroot()
            for quest in root.iter('Quest'):
                name_elem = quest.find('Name')
                qname = name_elem.text if name_elem is not None else ''
                desc = quest.find('Description')
                if desc is not None:
                    short_elem = desc.find('Short')
                    long_elem = desc.find('Long')
                    
                    # 翻译 Short
                    if short_elem is not None and short_elem.text:
                        for en_name, cn_short in QUEST_SHORT.items():
                            if qname == en_name:
                                short_elem.text = cn_short
                                break
                    
                    # 翻译 Long
                    if long_elem is not None and long_elem.text:
                        for en_name, cn_long in QUEST_LONG.items():
                            if qname == en_name:
                                long_elem.text = cn_long
                                break
            
            # 写出
            out_path = os.path.join(out_dir, fname)
            tree.write(out_path, encoding='utf-8', xml_declaration=False)
            print(f"  -> {out_path}")
        except Exception as e:
            print(f"  ERROR parsing {fname}: {e}")
            # 退回到原始文件复制
            out_path = os.path.join(out_dir, fname)
            with open(out_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"  -> {out_path} (copied original)")

# ============================================================
# 主处理流程
# ============================================================
process_interface_strings()
process_text_xml()
process_mqxml('Quests')
process_mqxml('QuestsMX')

print("\nDone! All XML files translated.")
