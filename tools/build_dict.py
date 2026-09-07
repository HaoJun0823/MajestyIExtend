#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建外挂汉化字典文件 (dict.txt)

从所有翻译资源中提取 英文原文 -> 中文翻译 映射,
输出为 UTF-8 文本文件, 格式: 英文\\t中文\\n

DLL 启动时加载此文件到内存 hash map, 运行时查表替换。

字典来源:
1. translate_cam.py 中的所有翻译表 (NEW_TRANSLATIONS, SUPPLEMENT, ROUND3, ROUND4, ROUND5)
2. glossary.py 中的 ALL_TERMS
3. translate_uidata.py 中的 UIDATA_TRANSLATIONS
4. translate_xml.py 中的 GUILD_NAMES, HERO_NAMES 等
5. M1 原版中文 CAM (orig_cn_map, 经术语替换)
6. 已翻译的 XML 文件 (Quests, InterfaceStrings) 中英文→中文对照
"""
import os
import sys
import json
import re
import xml.etree.ElementTree as ET

# ============================================================
# 路径
# ============================================================
WS = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace'
HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'
ORIG_GAME = r'G:\Projects\Majesty1'
TOOLS = os.path.join(WS, 'tools')

sys.path.insert(0, TOOLS)
os.chdir(TOOLS)

# ============================================================
# 1. 加载 translate_cam.py 中的翻译资源
# ============================================================
print("Loading translation resources...")

from glossary import TERM_REPLACE, ALL_TERMS
from round5_translations import ROUND5_TRANSLATIONS

# round3
with open(os.path.join(TOOLS, 'round3_translations.json'), 'r', encoding='utf-8') as f:
    r3 = json.load(f)
ROUND3_EXACT = r3.get('exact', {})

# round4
with open(os.path.join(TOOLS, 'round4_translations.json'), 'r', encoding='utf-8') as f:
    ROUND4_TRANSLATIONS = json.load(f)

# supplement
with open(os.path.join(TOOLS, 'supplement_translations.json'), 'r', encoding='utf-8') as f:
    SUPPLEMENT = json.load(f)

# translate_cam.py 中的 NEW_TRANSLATIONS (手动复制, 因为脚本有副作用)
NEW_TRANSLATIONS = {
    "Quest Data Missing": "任务数据缺失",
    "The Quest data used to create this save is missing.  The Quest will be skipped.": "用于创建此存档的任务数据缺失。该任务将被跳过。",
    "ACTIVATE MODS": "激活模组",
    "DEACTIVATE MODS": "停用模组",
    "Works with: Original Majesty Quests": "适用：原版任务",
    "Works with: Northern Expansion Quests": "适用：北境扩展任务",
    "Works with: Original and Northern Expansion Quests": "适用：原版及北境扩展任务",
    "Works with: Unknown Data Set": "适用：未知数据集",
    "Mods Missing": "模组缺失",
    "BARREN_WASTE": "荒芜之地",
    "BELL_BOOK_CANDLE": "钟书烛",
    "BRASHNARD": "布拉什纳德",
    "DARK_FOREST": "暗影森林",
    "DAY_OF_RECKONING": "清算之日",
    "DEAL_DEMON": "恶魔交易",
    "ELVEN_TREACHERY": "精灵的背叛",
    "FERTILE_PLAIN": "丰饶平原",
    "FORSAKEN_LANDS": "被遗弃之地",
    "FREE_SLAVES": "解放奴隶",
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
    "BALANCE_OF_TWILIGHT": "暮光之衡",
    "CLASH_OF_EMPIRES": "帝国冲突",
    "DARKNESS_FALLS": "黑暗降临",
    "FORTRESS_IXMIL": "伊克斯米尔要塞",
    "LEGENDARY_HEROES": "传奇英雄",
    "RISE_OF_RATMEN": "鼠人崛起",
    "SCIONS_OF_CHAOS": "混沌之子",
    "SIEGE": "围城",
    "SPIRES_OF_DEATH": "死亡之塔",
    "TRADE_ROUTES": "贸易路线",
    "URBAN_RENEWAL": "城市重建",
    "VALE_OF_SERPENTS": "毒蛇之谷",
    "VIGIL": "守夜",
    "Build a Fairgrounds to win this quest.": "建造一座竞技场即可完成任务。",
    "Recover the three items which have been stolen from your Palace.": "寻回从宫殿中被盗的三件物品。",
    "Recover the seven shards that make up Brashnard's Ultimate Sword.": "寻回组成布拉什纳德终极之剑的七块碎片。",
    "You have defeated the vampires!": "你击败了吸血鬼！",
    "You have lost to the vampires!": "你被吸血鬼击败了！",
}

# ============================================================
# 2. 加载 M1 原版中文 CAM (经术语替换 + 乱码检测)
# ============================================================
from cam_packer import read_cam, parse_strt

def is_garbled(en_text, cn_text):
    """检测 cn_text 是否是 ASCII 被误读为 UTF-16LE 产生的乱码"""
    if not en_text or not cn_text:
        return False
    try:
        en_text.encode('ascii')
    except (UnicodeEncodeError, AttributeError):
        return False
    has_cjk_ext_a = any(0x3400 <= ord(c) <= 0x4DBF for c in cn_text)
    has_real_cjk = any(0x4E00 <= ord(c) <= 0x9FFF for c in cn_text)
    if has_cjk_ext_a and not has_real_cjk:
        return True
    if has_cjk_ext_a and has_real_cjk:
        ext_a_count = sum(1 for c in cn_text if 0x3400 <= ord(c) <= 0x4DBF)
        real_count = sum(1 for c in cn_text if 0x4E00 <= ord(c) <= 0x9FFF)
        if ext_a_count > real_count:
            return True
    if '\ufffd' in cn_text:
        return True
    try:
        cn_bytes = cn_text.encode('utf-16-le')
    except (UnicodeEncodeError, AttributeError):
        return False
    decoded = cn_bytes.decode('ascii', errors='replace')
    decoded = decoded.replace('\xff\xfe', '').replace('\x00', '')
    en_clean = ''.join(c for c in en_text if c >= ' ')
    decoded_clean = ''.join(c for c in decoded if c >= ' ')
    if en_clean and decoded_clean:
        if en_clean == decoded_clean:
            return True
        if len(en_clean) >= 3 and decoded_clean.startswith(en_clean):
            return True
        if len(en_clean) > 3 and en_clean[:20] == decoded_clean[:20]:
            return True
    return False

def apply_term_replace(text):
    result = text
    for old, new in TERM_REPLACE.items():
        result = result.replace(old, new)
    return result

print("Loading M1 original Chinese CAM files...")
orig_cn_map = {}

def _load_m1_cam(cam_path, label):
    if not os.path.exists(cam_path):
        return
    m1_cam = read_cam(cam_path)
    for sec in m1_cam['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            for s in strings:
                orig_cn_map[(f['name'], s['id'])] = s['text']

_load_m1_cam(os.path.join(ORIG_GAME, 'Data', 'textdata.cam'), 'textdata.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'Data', 'gpltext.cam'), 'gpltext.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext.cam')
print(f"  M1 original Chinese entries: {len(orig_cn_map)}")

# ============================================================
# 3. 从 M1 CAM + HD 原版英文 CAM 建立英文→中文映射
# ============================================================
print("\nBuilding English->Chinese mapping from CAM files...")

# 读取 HD 原版英文 CAM 来获取英文原文
def _load_hd_cam_en(cam_path, label):
    """读取 HD 原版 CAM, 返回 (file_name, str_id) -> english_text"""
    en_map = {}
    if not os.path.exists(cam_path):
        return en_map
    cam = read_cam(cam_path)
    for sec in cam['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            for s in strings:
                en_map[(f['name'], s['id'])] = s['text']
    return en_map

hd_en_maps = {}
hd_en_maps['textdata'] = _load_hd_cam_en(os.path.join(HD_GAME, 'backup_original', 'Data', 'textdata.cam'), 'textdata')
hd_en_maps['gpltext'] = _load_hd_cam_en(os.path.join(HD_GAME, 'backup_original', 'Data', 'gpltext.cam'), 'gpltext')
hd_en_maps['mx_textdata'] = _load_hd_cam_en(os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_textdata.cam'), 'mx_textdata')
hd_en_maps['mx_gpltext'] = _load_hd_cam_en(os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext')
hd_en_maps['mx_rgstext'] = _load_hd_cam_en(os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext')

total_en = sum(len(v) for v in hd_en_maps.values())
print(f"  HD original English entries: {total_en}")

# 建立 英文→中文 映射
cam_dict = {}  # english_text -> chinese_text

for label, en_map in hd_en_maps.items():
    for key, en_text in en_map.items():
        cn_text = orig_cn_map.get(key)
        if cn_text:
            if is_garbled(en_text, cn_text):
                continue
            cn_text = apply_term_replace(cn_text)
            # 去控制字符
            en_clean = en_text.replace('\x01', '')
            if en_clean and cn_text and en_clean != cn_text:
                cam_dict[en_clean] = cn_text

print(f"  CAM-derived translations: {len(cam_dict)}")

# ============================================================
# 4. 合并所有翻译表
# ============================================================
print("\nMerging all translation tables...")
final_dict = {}

def normalize_key(text):
    """规范化 key: 去控制字符, 但保留原始大小写和空格"""
    if not text:
        return text
    return text.replace('\x01', '')

def deep_normalize(text):
    """深度规范化"""
    if not text:
        return text
    result = text.replace('\x01', '')
    result = result.replace('\ufffd', "'")
    result = result.replace('\u2019', "'")
    result = result.replace('\u2018', "'")
    result = result.replace('\u201c', '"')
    result = result.replace('\u201d', '"')
    result = re.sub(r'\n{3,}', '\n\n', result)
    result = '\n'.join(line.rstrip() for line in result.split('\n'))
    result = result.rstrip()
    result = result.replace('"', "'")
    return result

# 添加 CAM 翻译
for en, cn in cam_dict.items():
    final_dict[en] = cn

# 添加 NEW_TRANSLATIONS
for en, cn in NEW_TRANSLATIONS.items():
    final_dict[en] = cn
    final_dict[normalize_key(en)] = cn

# 添加 SUPPLEMENT
for en, cn in SUPPLEMENT.items():
    final_dict[en] = cn
    final_dict[normalize_key(en)] = cn

# 添加 ROUND3_EXACT
for en, cn in ROUND3_EXACT.items():
    final_dict[en] = cn
    final_dict[normalize_key(en)] = cn
    final_dict[deep_normalize(en)] = cn

# 添加 ROUND4
for en, cn in ROUND4_TRANSLATIONS.items():
    final_dict[en] = cn
    final_dict[normalize_key(en)] = cn
    final_dict[deep_normalize(en)] = cn

# 添加 ROUND5
for en, cn in ROUND5_TRANSLATIONS.items():
    final_dict[en] = cn
    final_dict[normalize_key(en)] = cn

# 添加术语表
for en, cn in ALL_TERMS.items():
    final_dict[en] = cn

# ============================================================
# 5. 从已翻译 XML 文件提取英文→中文对照
# ============================================================
print("\nExtracting translations from XML files...")

def extract_xml_translations(orig_dir, trans_dir):
    """对比原始和翻译后的 XML 文件, 提取英文→中文映射"""
    result = {}
    if not os.path.exists(orig_dir) or not os.path.exists(trans_dir):
        return result
    
    for fname in os.listdir(trans_dir):
        orig_path = os.path.join(orig_dir, fname)
        trans_path = os.path.join(trans_dir, fname)
        if not os.path.exists(orig_path) or not os.path.exists(trans_path):
            continue
        if not fname.endswith('.xml') and not fname.endswith('.mqxml'):
            continue
        
        try:
            orig_tree = ET.parse(orig_path)
            trans_tree = ET.parse(trans_path)
            orig_root = orig_tree.getroot()
            trans_root = trans_tree.getroot()
            
            # 提取所有文本节点
            def extract_texts(root):
                texts = []
                for elem in root.iter():
                    if elem.text and elem.text.strip():
                        texts.append((elem.tag, elem.attrib.get('id', ''), elem.text.strip()))
                return texts
            
            orig_texts = extract_texts(orig_root)
            trans_texts = extract_texts(trans_root)
            
            # 按 tag+id 配对
            trans_map = {}
            for tag, id_, text in trans_texts:
                trans_map[(tag, id_)] = text
            
            for tag, id_, en_text in orig_texts:
                cn_text = trans_map.get((tag, id_))
                if cn_text and en_text != cn_text:
                    result[en_text] = cn_text
                    result[normalize_key(en_text)] = cn_text
        except Exception as e:
            print(f"  Warning: failed to parse {fname}: {e}")
    
    return result

# InterfaceStrings.xml
xml_dict = extract_xml_translations(
    os.path.join(HD_GAME, 'backup_original', 'Data'),
    os.path.join(HD_GAME, 'workspace', 'output', 'Data')
)
print(f"  InterfaceStrings translations: {len(xml_dict)}")
for en, cn in xml_dict.items():
    final_dict[en] = cn

# Quests
xml_dict = extract_xml_translations(
    os.path.join(HD_GAME, 'backup_original', 'Quests'),
    os.path.join(HD_GAME, 'workspace', 'output', 'Quests')
)
print(f"  Quests translations: {len(xml_dict)}")
for en, cn in xml_dict.items():
    final_dict[en] = cn

# QuestsMX
xml_dict = extract_xml_translations(
    os.path.join(HD_GAME, 'backup_original', 'QuestsMX'),
    os.path.join(HD_GAME, 'workspace', 'output', 'QuestsMX')
)
print(f"  QuestsMX translations: {len(xml_dict)}")
for en, cn in xml_dict.items():
    final_dict[en] = cn

# ============================================================
# 6. 从 UIData 翻译表提取
# ============================================================
print("\nExtracting UIData translations...")

# 从 translate_uidata.py 中提取 UIDATA_TRANSLATIONS
from translate_uidata import UIDATA_TRANSLATIONS
print(f"  UIData translations: {len(UIDATA_TRANSLATIONS)}")
for en, cn in UIDATA_TRANSLATIONS.items():
    # 跳过内部按钮名（不翻译的）
    if en == cn:
        continue
    final_dict[en] = cn

# ============================================================
# 7. 清理和去重
# ============================================================
print("\nCleaning dictionary...")

# 去掉空 key 或空 value
clean_dict = {}
for en, cn in final_dict.items():
    en = en.strip() if en else ""
    cn = cn.strip() if cn else ""
    if en and cn and en != cn:
        # 跳过过长的 key（>500字符, 不太可能是查表文本）
        if len(en) > 500:
            continue
        clean_dict[en] = cn

print(f"  Final dictionary entries: {len(clean_dict)}")

# ============================================================
# 8. 输出字典文件
# ============================================================
output_path = os.path.join(HD_GAME, 'scripts', 'dict.txt')
os.makedirs(os.path.dirname(output_path), exist_ok=True)

# 零宽字符集合 (从输出中清除)
ZERO_WIDTH_CHARS = {
    '\u200B',  # ZERO WIDTH SPACE
    '\u200C',  # ZERO WIDTH NON-JOINER
    '\u200D',  # ZERO WIDTH JOINER
    '\u200E',  # LEFT-TO-RIGHT MARK
    '\u200F',  # RIGHT-TO-LEFT MARK
    '\uFEFF',  # ZERO WIDTH NO-BREAK SPACE (BOM)
    '\u2060',  # WORD JOINER
    '\u2061',  # FUNCTION APPLICATION
    '\u2062',  # INVISIBLE TIMES
    '\u2063',  # INVISIBLE SEPARATOR
    '\u2064',  # INVISIBLE PLUS
}

def strip_zero_width(text):
    """移除所有零宽字符"""
    return ''.join(c for c in text if c not in ZERO_WIDTH_CHARS)

# 用 UTF-8 BOM 写入, 帮助 C++ 端识别编码
with open(output_path, 'w', encoding='utf-8-sig', errors='replace') as f:
    for en, cn in sorted(clean_dict.items()):
        # 清理零宽字符
        en_clean = strip_zero_width(en)
        cn_clean = strip_zero_width(cn)
        if not en_clean or not cn_clean:
            continue
        # 用 tab 分隔, 换行分隔条目
        # 确保英文和中文中不包含 tab 和换行（替换为空格）
        en_clean = en_clean.replace('\t', ' ').replace('\n', '\\n').replace('\r', '')
        cn_clean = cn_clean.replace('\t', ' ').replace('\n', '\\n').replace('\r', '')
        f.write(f"{en_clean}\t{cn_clean}\n")

print(f"\nDictionary written to: {output_path}")
print(f"Total entries: {len(clean_dict)}")

# 统计
has_cjk = sum(1 for v in clean_dict.values() if any(0x4E00 <= ord(c) <= 0x9FFF for c in v))
print(f"Entries with CJK: {has_cjk}")
print("Done!")
