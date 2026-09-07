#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
第四轮翻译：处理剩余99条未翻译条目（完整文本匹配）

这些条目之前因为翻译表key被截断到80字符而无法匹配，
现在用完整文本作为key进行精确匹配。
"""
import os
import json

# 从JSON文件加载
_round4_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'round4_translations.json')
with open(_round4_path, 'r', encoding='utf-8') as f:
    ROUND4_TRANSLATIONS = json.load(f)

if __name__ == '__main__':
    print(f"Round 4 translations: {len(ROUND4_TRANSLATIONS)}")
