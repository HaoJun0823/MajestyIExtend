#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convert dict.txt (tab-separated) to dict.json"""
import json
import os

dict_txt = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts\dict.txt"
dict_json = r"I:\SteamLibrary\steamapps\common\Majesty HD\scripts\dict.json"

entries = {}
skipped = 0

with open(dict_txt, "r", encoding="utf-8") as f:
    for line_num, line in enumerate(f, 1):
        line = line.rstrip("\n").rstrip("\r")
        if not line:
            continue
        if "\t" not in line:
            skipped += 1
            continue
        # Split on first tab only
        parts = line.split("\t", 1)
        if len(parts) != 2:
            skipped += 1
            continue
        en, cn = parts
        if not en or not cn:
            skipped += 1
            continue
        # Convert literal \n in cn to actual newline for JSON (json.dump will escape it)
        cn = cn.replace("\\n", "\n")
        entries[en] = cn

with open(dict_json, "w", encoding="utf-8") as f:
    json.dump(entries, f, ensure_ascii=False, indent=0, separators=(",", ":"))

print(f"Converted {len(entries)} entries to {dict_json}")
print(f"Skipped {skipped} lines")
print(f"File size: {os.path.getsize(dict_json)} bytes")

# Verify by reading back
with open(dict_json, "r", encoding="utf-8") as f:
    verify = json.load(f)
print(f"Verify: {len(verify)} entries loaded back OK")
