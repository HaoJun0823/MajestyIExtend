#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
M1 HD 汉化翻译脚本

策略:
1. CAM STRT: 
   - 有原版中文对应的: 用原版中文做术语替换(->M2标准)
   - 无原版对应的: 直接翻译
2. XML 文件: 直接翻译
3. 所有翻译统一使用 UTF-16LE 格式打包

输出:
   - output/Data/  (汉化后的 CAM 文件)
   - output/DataMX/
   - output/Quests/ (汉化后的 XML 文件)
   - output/QuestsMX/
   - output/Data/InterfaceStrings.xml
"""
import os
import sys
import json
import struct
import re

# 添加工具路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam_packer import read_cam, parse_strt, build_strt, pack_cam
from glossary import TERM_REPLACE, ALL_TERMS
from round4_translations import ROUND4_TRANSLATIONS
from round5_translations import ROUND5_TRANSLATIONS

# ============================================================
# 路径
# ============================================================
WS = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace'
HD_GAME = r'I:\SteamLibrary\steamapps\common\Majesty HD'
ORIG_GAME = r'G:\Projects\Majesty1'

# 临时数据
TEMP = r'G:\Projects\Majesty1\.temp'

# ============================================================
# 直接读取 M1 原版中文 CAM 文件（用修复后的 parse_strt）
# ============================================================
orig_cn_map = {}  # (file_name, str_id) -> cn_text

def _load_m1_cam(cam_path, label):
    """读取 M1 CAM 文件，提取所有 STRT 文本到 orig_cn_map"""
    if not os.path.exists(cam_path):
        print(f"  M1 {label}: file not found, skipping")
        return
    m1_cam = read_cam(cam_path)
    count = 0
    for sec in m1_cam['sections']:
        if sec['ext'] != 'STRT':
            continue
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            for s in strings:
                orig_cn_map[(f['name'], s['id'])] = s['text']
                count += 1
    print(f"  M1 {label}: {count} entries loaded")

print("Loading M1 original Chinese CAM files...")
_load_m1_cam(os.path.join(ORIG_GAME, 'Data', 'textdata.cam'), 'textdata.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'Data', 'gpltext.cam'), 'gpltext.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_textdata.cam'), 'mx_textdata.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_gpltext.cam'), 'mx_gpltext.cam')
_load_m1_cam(os.path.join(ORIG_GAME, 'DataMX', 'mx_rgstext.cam'), 'mx_rgstext.cam')
print(f"Original Chinese entries total: {len(orig_cn_map)}")

# ============================================================
# 术语替换函数
# ============================================================
def apply_term_replace(text):
    """将原版中文术语替换为 M2 标准"""
    result = text
    for old, new in TERM_REPLACE.items():
        result = result.replace(old, new)
    return result

# ============================================================
# 翻译表：无原版中文对应的英文 -> 中文
# 这些是高清版新增的文本
# ============================================================
NEW_TRANSLATIONS = {
    # InterfaceStrings.xml
    "Quest Data Missing": "任务数据缺失",
    "The Quest data used to create this save is missing.  The Quest will be skipped.": "用于创建此存档的任务数据缺失。该任务将被跳过。",
    "ACTIVATE MODS": "激活模组",
    "DEACTIVATE MODS": "停用模组",
    "Works with: Original Majesty Quests": "适用：原版任务",
    "Works with: Northern Expansion Quests": "适用：北境扩展任务",
    "Works with: Original and Northern Expansion Quests": "适用：原版及北境扩展任务",
    "Works with: Unknown Data Set": "适用：未知数据集",
    "Mods Missing": "模组缺失",

    # Quest mqxml - 任务名称
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
    
    # QuestMX 任务名称
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
    
    # 任务描述 Short/Long - 选取重要文本
    "Build a Fairgrounds to win this quest.": "建造一座竞技场即可完成任务。",
    "Recover the three items which have been stolen from your Palace.": "寻回从宫殿中被盗的三件物品。",
    "Recover the seven shards that make up Brashnard's Ultimate Sword.": "寻回组成布拉什纳德终极之剑的七块碎片。",
    
    # Text.xml
    "You have defeated the vampires!": "你击败了吸血鬼！",
    "You have lost to the vampires!": "你被吸血鬼击败了！",
}

# 加载补充翻译表
SUPPLEMENT_TRANSLATIONS = {}
supplement_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'supplement_translations.json')
if os.path.exists(supplement_path):
    with open(supplement_path, 'r', encoding='utf-8') as f:
        SUPPLEMENT_TRANSLATIONS = json.load(f)
    print(f"Supplement translations loaded: {len(SUPPLEMENT_TRANSLATIONS)}")

# 加载第三轮翻译表（含模板匹配）
ROUND3_EXACT = {}
ROUND3_TEMPLATES = []
round3_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'round3_translations.json')
if os.path.exists(round3_path):
    with open(round3_path, 'r', encoding='utf-8') as f:
        r3 = json.load(f)
    ROUND3_EXACT = r3.get('exact', {})
    ROUND3_TEMPLATES = [(t[0], t[1]) for t in r3.get('templates', [])]
    print(f"Round 3 translations loaded: {len(ROUND3_EXACT)} exact + {len(ROUND3_TEMPLATES)} templates")
print(f"Round 4 translations loaded: {len(ROUND4_TRANSLATIONS)}")
print(f"Round 5 translations loaded: {len(ROUND5_TRANSLATIONS)}")

def normalize_text(text):
    """去除控制字符\x01等，用于匹配"""
    if not text:
        return text
    return text.replace('\x01', '')

def deep_normalize(text):
    """深度规范化：去除\x01 + 统一引号 + 统一多换行 + 去空格差异"""
    if not text:
        return text
    result = text.replace('\x01', '')
    # 统一所有引号为ASCII（包括\ufffd，因为ASCII decoder把CP1252引号全变\ufffd）
    result = result.replace('\ufffd', "'")
    result = result.replace('\u2019', "'")
    result = result.replace('\u2018', "'")
    result = result.replace('\u201c', '"')
    result = result.replace('\u201d', '"')
    # 统一连续3+换行为2个换行
    result = re.sub(r'\n{3,}', '\n\n', result)
    # 去每行尾部空格
    result = '\n'.join(line.rstrip() for line in result.split('\n'))
    # 去整体尾部空格
    result = result.rstrip()
    # 统一单双引号：实际CAM文本中\ufffd统一变'，翻译表key中可能用"——都统一为'
    # 这样可以匹配实际文本中的 ' 和翻译表key中的 " 
    result = result.replace('"', "'")
    return result

# 构建深度规范化后的翻译表（key也做deep_normalize）
ROUND4_DEEP = {}
for k, v in ROUND4_TRANSLATIONS.items():
    ROUND4_DEEP[deep_normalize(k)] = v

ROUND3_EXACT_DEEP = {}
for k, v in ROUND3_EXACT.items():
    ROUND3_EXACT_DEEP[deep_normalize(k)] = v

SUPPLEMENT_DEEP = {}
for k, v in SUPPLEMENT_TRANSLATIONS.items():
    SUPPLEMENT_DEEP[deep_normalize(k)] = v

NEW_TRANS_DEEP = {}
for k, v in NEW_TRANSLATIONS.items():
    NEW_TRANS_DEEP[deep_normalize(k)] = v

ROUND5_DEEP = {}
for k, v in ROUND5_TRANSLATIONS.items():
    ROUND5_DEEP[deep_normalize(k)] = v

def is_garbled(en_text, cn_text):
    """检测 cn_text 是否是 ASCII 字节被误读为 UTF-16LE 产生的乱码。
    
    M1 原版中文的部分 STRT 文件（GDB1/HKTX/UNTN/AP93/APd6 等）存在格式矛盾：
    flags=0x0208（UTF-16LE）但数据区是 ASCII 编码。
    parse_strt 按 UTF-16LE 解析后产生乱码文本。
    这些乱码被存入 m1_extracted.json 后被当作"翻译"使用。
    
    两种检测方法并用：
    A. 字节还原法：把 cn_text 按 UTF-16LE 编码回字节再按 ASCII 解码，
       如果结果与 en_text 相似，则确认是乱码。
    B. 字符区间法：如果 cn_text 含有 CJK Extension A 区字符
       (U+3400-U+4DBF) 且 en_text 是纯 ASCII，则很可能是乱码
       （正常中文翻译不会出现 CJK Ext A 字符）。
    """
    if not en_text or not cn_text:
        return False
    # 原始英文必须是纯 ASCII（含控制字符）
    try:
        en_text.encode('ascii')
    except (UnicodeEncodeError, AttributeError):
        return False
    
    # 方法 B: 字符区间法
    # 正常中文翻译不会出现 CJK Extension A (U+3400-U+4DBF)
    # 乱码文本中大量出现这个区间的字符
    has_cjk_ext_a = any(0x3400 <= ord(c) <= 0x4DBF for c in cn_text)
    has_real_cjk = any(0x4E00 <= ord(c) <= 0x9FFF for c in cn_text)
    if has_cjk_ext_a and not has_real_cjk:
        return True
    # 如果同时有 CJK Ext A 和真正的 CJK，检查比例
    if has_cjk_ext_a and has_real_cjk:
        ext_a_count = sum(1 for c in cn_text if 0x3400 <= ord(c) <= 0x4DBF)
        real_count = sum(1 for c in cn_text if 0x4E00 <= ord(c) <= 0x9FFF)
        if ext_a_count > real_count:
            return True
    
    # 方法 C: U+FFFD 检测法
    # 如果 cn_text 含 U+FFFD 替换字符且 en_text 是纯 ASCII，很可能是乱码
    # （正常中文翻译不应出现 U+FFFD）
    if '\ufffd' in cn_text:
        # 排除 en_text 本身含 non-ASCII 的情况
        return True
    
    # 方法 A: 字节还原法
    try:
        cn_bytes = cn_text.encode('utf-16-le')
    except (UnicodeEncodeError, AttributeError):
        return False
    decoded = cn_bytes.decode('ascii', errors='replace')
    decoded = decoded.replace('\xff\xfe', '').replace('\x00', '')
    en_clean = ''.join(c for c in en_text if c >= ' ')
    decoded_clean = ''.join(c for c in decoded if c >= ' ')
    if en_clean and decoded_clean:
        # 完全匹配
        if en_clean == decoded_clean:
            return True
        # en_clean 是 decoded_clean 的前缀（乱码解码后可能多出尾部字符）
        if len(en_clean) >= 3 and decoded_clean.startswith(en_clean):
            return True
        # 前20字符匹配
        if len(en_clean) > 3 and en_clean[:20] == decoded_clean[:20]:
            return True
        # en_clean 的前缀是 decoded_clean 的前缀（en 比 decoded 短时）
        if len(en_clean) > 3 and decoded_clean.startswith(en_clean[:min(len(en_clean), 20)]):
            return True
        # 短文本（1-3字符）：如果 decoded_clean 以 en_clean 开头且后面有额外字符
        if 1 <= len(en_clean) <= 3 and decoded_clean.startswith(en_clean) and len(decoded_clean) > len(en_clean):
            return True
    return False
    # 原始英文必须是纯 ASCII（含控制字符）
    try:
        en_bytes = en_text.encode('ascii')
    except (UnicodeEncodeError, AttributeError):
        return False
    # 把 cn_text 编码为 UTF-16LE 字节
    try:
        cn_bytes = cn_text.encode('utf-16-le')
    except (UnicodeEncodeError, AttributeError):
        return False
    # 按 ASCII 解码（忽略错误）
    decoded = cn_bytes.decode('ascii', errors='replace')
    # 去掉 BOM 和 null 字节
    decoded = decoded.replace('\xff\xfe', '').replace('\x00', '')
    # 去掉控制字符
    en_clean = ''.join(c for c in en_text if c >= ' ')
    decoded_clean = ''.join(c for c in decoded if c >= ' ')
    # 如果相似度高（前缀匹配或完全匹配），则是乱码
    if en_clean and decoded_clean and en_clean == decoded_clean:
        return True
    # 也检查前缀匹配（处理控制字符差异）
    if len(en_clean) > 3 and en_clean[:20] == decoded_clean[:20]:
        return True
    return False


def translate_text(en_text, file_name=None, str_id=None):
    """翻译英文文本
    
    优先级:
    1. 查找原版中文对应 -> 术语替换（含乱码检测）
    2. 查找新翻译表
    3. 查找补充翻译表
    4. 查找第三轮精确翻译
    5. 查找第四轮完整文本翻译
    6. 用前80字符截断匹配
    7. 查找第三轮模板匹配（startswith）
    8. 查找术语表直接匹配
    9. 返回None（待后续处理）
    """
    # 规范化文本（去除\x01控制字符）
    norm_text = normalize_text(en_text)
    # 深度规范化（修复智能引号+统一换行）
    deep_text = deep_normalize(en_text)
    
    # 1. 原版中文 + 术语替换（含乱码检测）
    if file_name and str_id is not None:
        orig_cn = orig_cn_map.get((file_name, str_id))
        if orig_cn:
            # 检测是否是 M1 格式矛盾导致的乱码
            if is_garbled(en_text, orig_cn):
                pass  # 跳过乱码"翻译"，继续到下一优先级
            else:
                return apply_term_replace(orig_cn)
    
    # 2. 新翻译表
    if en_text in NEW_TRANSLATIONS:
        return NEW_TRANSLATIONS[en_text]
    if norm_text in NEW_TRANSLATIONS:
        return NEW_TRANSLATIONS[norm_text]
    if deep_text in NEW_TRANSLATIONS:
        return NEW_TRANSLATIONS[deep_text]
    if deep_text in NEW_TRANS_DEEP:
        return NEW_TRANS_DEEP[deep_text]
    
    # 3. 补充翻译表
    if en_text in SUPPLEMENT_TRANSLATIONS:
        return SUPPLEMENT_TRANSLATIONS[en_text]
    if norm_text in SUPPLEMENT_TRANSLATIONS:
        return SUPPLEMENT_TRANSLATIONS[norm_text]
    if deep_text in SUPPLEMENT_TRANSLATIONS:
        return SUPPLEMENT_TRANSLATIONS[deep_text]
    if deep_text in SUPPLEMENT_DEEP:
        return SUPPLEMENT_DEEP[deep_text]
    
    # 4. 第三轮精确翻译
    if en_text in ROUND3_EXACT:
        return ROUND3_EXACT[en_text]
    if norm_text in ROUND3_EXACT:
        return ROUND3_EXACT[norm_text]
    if deep_text in ROUND3_EXACT:
        return ROUND3_EXACT[deep_text]
    if deep_text in ROUND3_EXACT_DEEP:
        return ROUND3_EXACT_DEEP[deep_text]
    
    # 5. 第四轮完整文本翻译（用完整文本精确匹配）
    if en_text in ROUND4_TRANSLATIONS:
        return ROUND4_TRANSLATIONS[en_text]
    if norm_text in ROUND4_TRANSLATIONS:
        return ROUND4_TRANSLATIONS[norm_text]
    if deep_text in ROUND4_TRANSLATIONS:
        return ROUND4_TRANSLATIONS[deep_text]
    if deep_text in ROUND4_DEEP:
        return ROUND4_DEEP[deep_text]
    
    # 5.5 第五轮翻译（法术/属性/多人聊天/月份/多人协议等）
    if en_text in ROUND5_TRANSLATIONS:
        return ROUND5_TRANSLATIONS[en_text]
    if norm_text in ROUND5_TRANSLATIONS:
        return ROUND5_TRANSLATIONS[norm_text]
    if deep_text in ROUND5_TRANSLATIONS:
        return ROUND5_TRANSLATIONS[deep_text]
    if deep_text in ROUND5_DEEP:
        return ROUND5_DEEP[deep_text]
    
    # 6. 用前80字符匹配（处理完整文本比翻译表key更长的情况）
    for prefix in [norm_text, deep_text]:
        if prefix and len(prefix) > 80:
            trunc = prefix[:80]
            if trunc in ROUND3_EXACT:
                return ROUND3_EXACT[trunc]
            if trunc in ROUND3_EXACT_DEEP:
                return ROUND3_EXACT_DEEP[trunc]
    
    # 7. 第三轮模板匹配（用前50字符匹配）
    if deep_text and len(deep_text) > 20:
        norm_prefix = deep_text[:50]
        for template_prefix, cn in ROUND3_TEMPLATES:
            if deep_text.startswith(template_prefix[:50]):
                return cn
    
    # 8. 术语表直接匹配（大小写不敏感）
    en_lower = deep_text.lower() if deep_text else ""
    for en_term, cn_term in ALL_TERMS.items():
        if en_term.lower() == en_lower:
            return cn_term
    
    # 9. 无法翻译，返回None
    return None

# ============================================================
# 处理 CAM 文件
# ============================================================
def process_cam_file(cam_path, source_key, label):
    """处理单个 CAM 文件: 读取 -> 翻译 -> 构建新 STRT -> 返回替换映射"""
    print(f"\n{'='*60}")
    print(f"Processing: {label}")
    print(f"{'='*60}")
    
    cam_data = read_cam(cam_path)
    
    replacements = {}
    stats = {'total': 0, 'translated': 0, 'untranslated': 0, 'fallback': 0}
    untranslated = []
    
    for sec in cam_data['sections']:
        if sec['ext'] != 'STRT':
            continue
        
        for f in sec['files']:
            strings = parse_strt(f['raw'])
            if not strings:
                continue
            
            new_strings = []
            for s in strings:
                stats['total'] += 1
                en = s['text']
                cn = translate_text(en, f['name'], s['id'])
                
                if cn is not None:
                    new_strings.append({'id': s['id'], 'text': cn})
                    stats['translated'] += 1
                else:
                    # 尝试术语替换（即使原文英文也可以直接用术语表）
                    cn_attempt = apply_term_replace(en)
                    if cn_attempt != en:
                        new_strings.append({'id': s['id'], 'text': cn_attempt})
                        stats['translated'] += 1
                    else:
                        # 无法翻译，保留英文
                        new_strings.append({'id': s['id'], 'text': en})
                        stats['untranslated'] += 1
                        if en and len(en) > 1:
                            untranslated.append({
                                'file': f['name'], 'id': s['id'], 'text': en[:80]
                            })
            
            # 构建新的 STRT 二进制 (UTF-16LE格式，flags=0x0208)
            new_raw = build_strt(new_strings, force_utf16=True, force_utf8=False)
            replacements[(sec['ext'], f['name'])] = new_raw
    
    print(f"  Total: {stats['total']}")
    print(f"  Translated: {stats['translated']}")
    print(f"  Untranslated: {stats['untranslated']}")
    
    if untranslated:
        print(f"\n  Untranslated entries ({len(untranslated)}):")
        for u in untranslated[:30]:
            print(f"    [{u['file']}#{u['id']}] {u['text']}")
        if len(untranslated) > 30:
            print(f"    ... and {len(untranslated) - 30} more")
    
    return cam_data, replacements, stats, untranslated

# ============================================================
# 主处理流程
# ============================================================
all_untranslated = []
all_stats = {}

# 1. textdata.cam
cam_data_td, repl_td, stats_td, untr_td = process_cam_file(
    os.path.join(HD_GAME, 'backup_original', 'Data', 'textdata.cam'),
    'hd_textdata',
    'HD textdata.cam'
)
all_untranslated.extend(untr_td)
all_stats['textdata'] = stats_td

# 2. gpltext.cam
cam_data_gpl, repl_gpl, stats_gpl, untr_gpl = process_cam_file(
    os.path.join(HD_GAME, 'backup_original', 'Data', 'gpltext.cam'),
    'hd_gpltext',
    'HD gpltext.cam'
)
all_untranslated.extend(untr_gpl)
all_stats['gpltext'] = stats_gpl

# 3. mx_textdata.cam
cam_data_mx_td, repl_mx_td, stats_mx_td, untr_mx_td = process_cam_file(
    os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_textdata.cam'),
    'hd_mx_textdata',
    'HD MX textdata.cam'
)
all_untranslated.extend(untr_mx_td)
all_stats['mx_textdata'] = stats_mx_td

# 4. mx_gpltext.cam
cam_data_mx_gpl, repl_mx_gpl, stats_mx_gpl, untr_mx_gpl = process_cam_file(
    os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_gpltext.cam'),
    'hd_mx_gpltext',
    'HD MX gpltext.cam'
)
all_untranslated.extend(untr_mx_gpl)
all_stats['mx_gpltext'] = stats_mx_gpl

# 5. mx_rgstext.cam
cam_data_mx_rgs, repl_mx_rgs, stats_mx_rgs, untr_mx_rgs = process_cam_file(
    os.path.join(HD_GAME, 'backup_original', 'DataMX', 'mx_rgstext.cam'),
    'hd_mx_rgstext',
    'HD MX rgstext.cam'
)
all_untranslated.extend(untr_mx_rgs)
all_stats['mx_rgstext'] = stats_mx_rgs

# 汇总
print(f"\n{'='*60}")
print(f"翻译汇总")
print(f"{'='*60}")
total_all = 0
trans_all = 0
untrans_all = 0
for name, s in all_stats.items():
    print(f"  {name}: total={s['total']} translated={s['translated']} untrans={s['untranslated']}")
    total_all += s['total']
    trans_all += s['translated']
    untrans_all += s['untranslated']
print(f"  TOTAL: total={total_all} translated={trans_all} untrans={untrans_all}")

# 保存未翻译列表
untrans_path = os.path.join(WS, 'tools', 'untranslated.json')
with open(untrans_path, 'w', encoding='utf-8') as f:
    json.dump(all_untranslated, f, ensure_ascii=False, indent=2)
print(f"\nUntranslated list saved to: {untrans_path}")

# 保存 cam_data 和 replacements 到临时文件（用于打包步骤）
# 但因为数据太大，我们直接在这里打包
print(f"\n{'='*60}")
print(f"打包 CAM 文件")
print(f"{'='*60}")

output_base = os.path.join(WS, 'output')
os.makedirs(os.path.join(output_base, 'Data'), exist_ok=True)
os.makedirs(os.path.join(output_base, 'DataMX'), exist_ok=True)

# 打包 textdata.cam
print("\nPacking textdata.cam...")
new_td = pack_cam(cam_data_td, repl_td)
out_path = os.path.join(output_base, 'Data', 'textdata.cam')
with open(out_path, 'wb') as f:
    f.write(new_td)
print(f"  -> {out_path} ({len(new_td)} bytes)")

# 打包 gpltext.cam
print("\nPacking gpltext.cam...")
new_gpl = pack_cam(cam_data_gpl, repl_gpl)
out_path = os.path.join(output_base, 'Data', 'gpltext.cam')
with open(out_path, 'wb') as f:
    f.write(new_gpl)
print(f"  -> {out_path} ({len(new_gpl)} bytes)")

# 打包 mx_textdata.cam
print("\nPacking mx_textdata.cam...")
new_mx_td = pack_cam(cam_data_mx_td, repl_mx_td)
out_path = os.path.join(output_base, 'DataMX', 'mx_textdata.cam')
with open(out_path, 'wb') as f:
    f.write(new_mx_td)
print(f"  -> {out_path} ({len(new_mx_td)} bytes)")

# 打包 mx_gpltext.cam
print("\nPacking mx_gpltext.cam...")
new_mx_gpl = pack_cam(cam_data_mx_gpl, repl_mx_gpl)
out_path = os.path.join(output_base, 'DataMX', 'mx_gpltext.cam')
with open(out_path, 'wb') as f:
    f.write(new_mx_gpl)
print(f"  -> {out_path} ({len(new_mx_gpl)} bytes)")

# 打包 mx_rgstext.cam
print("\nPacking mx_rgstext.cam...")
new_mx_rgs = pack_cam(cam_data_mx_rgs, repl_mx_rgs)
out_path = os.path.join(output_base, 'DataMX', 'mx_rgstext.cam')
with open(out_path, 'wb') as f:
    f.write(new_mx_rgs)
print(f"  -> {out_path} ({len(new_mx_rgs)} bytes)")

print("\nDone! All CAM files packed.")
