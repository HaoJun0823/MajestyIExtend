#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
M1 HD CAM 文件打包工具

读取 CAM 文件 -> 解析结构 -> 替换文本 -> 写回 CAM 文件

支持 STRT section 的 ASCII 和 UTF-16LE 两种格式。
打包时可以选择：
  - 保留原编码格式（ASCII 文件改用 ASCII 中文？不行）
  - 统一改为 UTF-16LE 格式（推荐，中文字符需要双字节）

策略：
  高清版原文件是 ASCII 格式(flags=0x0200)
  汉化后改为 UTF-16LE 格式(flags=0x0208)
  因为中文字符无法用 ASCII 表示

STRT 文件格式:
  Header (16 bytes): count(2) + flags(2) + u1(4) + u2(4) + u3(4)
    flags: 0x0200=ASCII, 0x0208=UTF-16LE
    u1 = 16 + (count-3)*4 (当 count > 3, 有偏移表)
       = count*4 + 4     (当 count <= 3, 无偏移表)
    u2 = 数据区第一个字符串偏移 (不确定, 仅参考)
    u3 = 不确定
  
  无偏移表 (count <= 3 或 u1 <= 16):
    数据: [text+null] [ID(4)+text+null] ...
    第一个字符串无 ID
  
  有偏移表 (count > 3 且 u1 > 16):
    偏移表: (count-3) 个 4 字节条目, 从 byte 16 开始
    数据区从 u1 开始:
      前3个字符串: [ID(4)+text+null] x 3
      后续: 由偏移表指向, 每个是 [ID(4)+text+null]
  
  UTF-16LE 变体: text 前有 FFFE BOM, null terminator 是 0000
"""
import struct
import os
import json
import copy


def _find_utf16_null(raw, start):
    """在 UTF-16LE 数据中搜索 null terminator (\x00\x00)，只在偶数偏移处匹配。

    避免误匹配：UTF-16LE 中如 U+5F00 (开) 编码为 00 5F，
    其首字节 0x00 与前一个字符（如空格 20 00）的尾字节 0x00
    会形成虚假的 \x00\x00，导致 raw.find 提前截断文本。
    """
    pos = start if start % 2 == 0 else start + 1
    while pos + 1 < len(raw):
        if raw[pos] == 0 and raw[pos + 1] == 0:
            return pos
        pos += 2
    return -1

def read_cam(filepath):
    """读取 CAM 文件，返回完整二进制 + 解析后的结构"""
    with open(filepath, 'rb') as f:
        data = bytearray(f.read())
    
    if data[0:6] != b'CYLBPC':
        raise ValueError(f"Not a CAM file: {filepath}")
    
    num_sections = struct.unpack_from('<I', data, 12)[0]
    
    # 解析 section 表
    pos = 20
    sections = []
    for i in range(num_sections):
        ext = data[pos:pos+4].decode('ascii', errors='replace').strip('\x00')
        offset = struct.unpack_from('<I', data, pos+4)[0]
        sections.append({'ext': ext, 'offset': offset, 'ext_raw': data[pos:pos+4], 'offset_pos': pos+4})
        pos += 8
    
    # 解析每个 section 的文件
    for sec in sections:
        sec_offset = sec['offset']
        num_files = struct.unpack_from('<I', data, sec_offset)[0]
        
        pos2 = sec_offset + 8
        files = []
        for j in range(num_files):
            name = data[pos2:pos2+20].decode('ascii', errors='replace').strip('\x00')
            file_offset = struct.unpack_from('<I', data, pos2+20)[0]
            file_size = struct.unpack_from('<I', data, pos2+24)[0]
            files.append({
                'name': name,
                'offset': file_offset,
                'size': file_size,
                'name_pos': pos2,
                'offset_pos': pos2+20,
                'size_pos': pos2+24,
                'raw': bytes(data[file_offset:file_offset+file_size])
            })
            pos2 += 28
        sec['files'] = files
    
    return {
        'data': data,
        'num_sections': num_sections,
        'sections': sections,
    }


def parse_strt(raw):
    """解析 STRT 文件，返回 [{id, text, offset_in_file}]"""
    if len(raw) < 16:
        return []
    
    count = struct.unpack_from('<H', raw, 0)[0]
    flags = struct.unpack_from('<H', raw, 2)[0]
    is_utf16 = (flags & 0x08) != 0
    u1 = struct.unpack_from('<I', raw, 4)[0]
    
    if u1 > 16 and count > 3:
        num_offsets = (u1 - 16) // 4
    else:
        num_offsets = 0
    
    data_start = u1 if u1 > 16 else 16
    
    offset_table = []
    for i in range(num_offsets):
        off = struct.unpack_from('<I', raw, 16 + i * 4)[0]
        offset_table.append(off)
    
    strings = []
    
    if is_utf16:
        if num_offsets == 0:
            # 无偏移表
            pos = 16
            if count >= 3:
                # count >= 3: 所有字符串都有 ID（包括第一个）
                for i in range(count):
                    if pos + 4 > len(raw):
                        break
                    str_id = struct.unpack_from('<I', raw, pos)[0]
                    text_offset = pos
                    pos += 4
                    if pos + 2 <= len(raw) and raw[pos:pos+2] == b'\xff\xfe':
                        pos += 2
                    end = _find_utf16_null(raw, pos)
                    if end == -1:
                        end = len(raw)
                    if end % 2 == 1:
                        end += 1
                    text = raw[pos:end].decode('utf-16-le', errors='replace')
                    strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                    pos = end + 2
            else:
                # count < 3: 第一个字符串无 ID
                if pos + 2 <= len(raw) and raw[pos:pos+2] == b'\xff\xfe':
                    pos += 2
                end = _find_utf16_null(raw, pos)
                if end == -1:
                    end = len(raw)
                if end % 2 == 1:
                    end += 1
                text = raw[pos:end].decode('utf-16-le', errors='replace')
                strings.append({'id': 0, 'text': text, 'offset': 16})
                pos = end + 2
                
                while pos + 4 < len(raw) and len(strings) < count:
                    str_id = struct.unpack_from('<I', raw, pos)[0]
                    text_offset = pos
                    pos += 4
                    if pos + 2 <= len(raw) and raw[pos:pos+2] == b'\xff\xfe':
                        pos += 2
                    end = _find_utf16_null(raw, pos)
                    if end == -1:
                        break
                    if end % 2 == 1:
                        end += 1
                    text = raw[pos:end].decode('utf-16-le', errors='replace')
                    strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                    pos = end + 2
        else:
            # 有偏移表
            # 前3个字符串从 data_start 开始
            pos = data_start
            for i in range(min(3, count)):
                if pos + 4 > len(raw):
                    break
                str_id = struct.unpack_from('<I', raw, pos)[0]
                text_offset = pos
                pos += 4
                if pos + 2 <= len(raw) and raw[pos:pos+2] == b'\xff\xfe':
                    pos += 2
                end = _find_utf16_null(raw, pos)
                if end == -1:
                    end = len(raw)
                if end % 2 == 1:
                    end += 1
                text = raw[pos:end].decode('utf-16-le', errors='replace')
                strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                pos = end + 2
            
            # 偏移表指向的条目
            for off in offset_table:
                if off + 4 > len(raw):
                    continue
                str_id = struct.unpack_from('<I', raw, off)[0]
                text_start = off + 4
                if text_start + 2 <= len(raw) and raw[text_start:text_start+2] == b'\xff\xfe':
                    text_start += 2
                end = _find_utf16_null(raw, text_start)
                if end == -1:
                    end = len(raw)
                if end % 2 == 1:
                    end += 1
                text = raw[text_start:end].decode('utf-16-le', errors='replace')
                strings.append({'id': str_id, 'text': text, 'offset': off})
    else:
        # ASCII 模式
        if num_offsets == 0:
            # 无偏移表
            pos = 16
            if count >= 3:
                # count >= 3: 所有字符串都有 ID（包括第一个）
                for i in range(count):
                    if pos + 4 > len(raw):
                        break
                    str_id = struct.unpack_from('<I', raw, pos)[0]
                    text_offset = pos
                    pos += 4
                    end = raw.find(b'\x00', pos)
                    if end == -1:
                        end = len(raw)
                    text = raw[pos:end].decode('ascii', errors='replace')
                    strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                    pos = end + 1
            else:
                # count < 3: 第一个字符串无 ID
                end = raw.find(b'\x00', pos)
                if end == -1:
                    return strings
                text = raw[pos:end].decode('ascii', errors='replace')
                strings.append({'id': 0, 'text': text, 'offset': 16})
                pos = end + 1
                
                while pos + 4 < len(raw) and len(strings) < count:
                    str_id = struct.unpack_from('<I', raw, pos)[0]
                    text_offset = pos
                    pos += 4
                    end = raw.find(b'\x00', pos)
                    if end == -1:
                        break
                    text = raw[pos:end].decode('ascii', errors='replace')
                    strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                    pos = end + 1
        else:
            # 有偏移表
            # 前3个字符串
            pos = data_start
            for i in range(min(3, count)):
                if pos + 4 > len(raw):
                    break
                str_id = struct.unpack_from('<I', raw, pos)[0]
                text_offset = pos
                pos += 4
                end = raw.find(b'\x00', pos)
                if end == -1:
                    end = len(raw)
                text = raw[pos:end].decode('ascii', errors='replace')
                strings.append({'id': str_id, 'text': text, 'offset': text_offset})
                pos = end + 1
            
            # 偏移表指向的条目
            for off in offset_table:
                if off + 4 > len(raw):
                    continue
                str_id = struct.unpack_from('<I', raw, off)[0]
                text_start = off + 4
                end = raw.find(b'\x00', text_start)
                if end == -1:
                    end = len(raw)
                text = raw[text_start:end].decode('ascii', errors='replace')
                strings.append({'id': str_id, 'text': text, 'offset': off})
    
    return strings


def build_strt(strings, force_utf16=True, force_utf8=False):
    """根据字符串列表构建 STRT 文件二进制数据
    
    参数:
        strings: [{id, text}] 列表
        force_utf16: 是否强制使用 UTF-16LE 格式 (flags=0x0208)
        force_utf8: 是否强制使用 UTF-8 编码 (flags=0x0200，但文本用UTF-8)
                    适用于引擎只读窄字符但有TextFix.asi转宽字符渲染的场景
    
    返回: bytes
    """
    count = len(strings)
    
    # 判断格式
    if force_utf16:
        flags = 0x0208
        is_utf16 = True
        is_utf8 = False
    elif force_utf8:
        # UTF-8 模式: 保持 flags=0x0200 (ASCII格式)，但文本用UTF-8编码
        # UTF-8编码的中文不含0x00字节，兼容null终止
        flags = 0x0200
        is_utf16 = False
        is_utf8 = True
    else:
        # 检查是否有非 ASCII 字符
        has_non_ascii = any(any(ord(c) > 127 for c in s['text']) for s in strings)
        if has_non_ascii:
            flags = 0x0208
            is_utf16 = True
            is_utf8 = False
        else:
            flags = 0x0200
            is_utf16 = False
            is_utf8 = False
    
    def encode_text(text):
        """编码单个字符串，返回 bytes (含 BOM 和 null terminator)"""
        if is_utf16:
            return b'\xff\xfe' + text.encode('utf-16-le') + b'\x00\x00'
        elif is_utf8:
            # UTF-8编码，null终止
            return text.encode('utf-8') + b'\x00'
        else:
            return text.encode('ascii', errors='replace') + b'\x00'
    
    def encode_entry(str_id, text):
        """编码一个条目: ID(4) + text + null"""
        return struct.pack('<I', str_id) + encode_text(text)
    
    # 构建数据
    if count < 3:
        # 无偏移表, 第一个字符串无 ID (count < 3)
        # 格式: [text0+null] [ID(4)+text1+null] ...
        data_parts = []
        
        # 第一个字符串 (无 ID)
        first_data = encode_text(strings[0]['text'])
        data_parts.append(first_data)
        
        # 后续字符串
        entry_datas = []
        for s in strings[1:]:
            entry_datas.append(encode_entry(s['id'], s['text']))
        data_parts.extend(entry_datas)
        
        data = b''.join(data_parts)
        
        # Header
        u1 = count * 4 + 4  # = (count - 3) * 4 + 16 when count >= 3
        # u2 = offset of string 1 (first string with ID), u3 = offset of string 2
        u2 = 16 + len(first_data) if count >= 2 else 0
        u3 = u2 + len(entry_datas[0]) if count >= 3 else 0
        
        header = struct.pack('<H', count) + struct.pack('<H', flags) + struct.pack('<I', u1) + struct.pack('<I', u2) + struct.pack('<I', u3)
        return header + data
    
    elif count == 3:
        # 无偏移表, 但所有字符串都有 ID (count == 3)
        # 格式: [ID(4)+text0+null] [ID(4)+text1+null] [ID(4)+text2+null]
        all_entries = []
        for s in strings:
            all_entries.append(encode_entry(s['id'], s['text']))
        data = b''.join(all_entries)
        
        # Header: u1 = 16 (无偏移表), u2/u3 = 字符串1/2的偏移
        u1 = 16
        u2 = 16 + len(all_entries[0])
        u3 = u2 + len(all_entries[1])
        
        header = struct.pack('<H', count) + struct.pack('<H', flags) + struct.pack('<I', u1) + struct.pack('<I', u2) + struct.pack('<I', u3)
        return header + data
    
    else:
        # 有偏移表 (count > 3)
        # 前3个字符串在数据区开头
        # 后 (count-3) 个由偏移表指向
        
        # 构建前3个条目
        first_entries_list = []
        for i in range(min(3, count)):
            first_entries_list.append(encode_entry(strings[i]['id'], strings[i]['text']))
        first_entries = b''.join(first_entries_list)
        
        # 构建后续条目
        later_entries = []
        for i in range(3, count):
            later_entries.append(encode_entry(strings[i]['id'], strings[i]['text']))
        
        # 计算偏移表大小
        num_offsets = count - 3
        offset_table_size = num_offsets * 4
        
        # 数据区起始 = 16 (header) + offset_table_size
        data_start = 16 + offset_table_size
        
        # 前3个条目从 data_start 开始
        # u2 = offset of string 1 (2nd entry in first_entries)
        # u3 = offset of string 2 (3rd entry in first_entries)
        u2 = data_start + len(first_entries_list[0]) if count >= 2 else 0
        u3 = u2 + len(first_entries_list[1]) if count >= 3 else 0
        
        # 后续条目偏移 = data_start + len(first_entries) + 前面所有 later_entries 的长度
        offset_table = []
        current_offset = data_start + len(first_entries)
        for entry in later_entries:
            offset_table.append(current_offset)
            current_offset += len(entry)
        
        # 构建偏移表
        offset_table_data = b''
        for off in offset_table:
            offset_table_data += struct.pack('<I', off)
        
        # Header
        u1 = data_start  # = 16 + (count-3)*4
        
        header = struct.pack('<H', count) + struct.pack('<H', flags) + struct.pack('<I', u1) + struct.pack('<I', u2) + struct.pack('<I', u3)
        
        # 组装
        return header + offset_table_data + first_entries + b''.join(later_entries)


def pack_cam(orig_data, replacements):
    """打包 CAM 文件
    
    参数:
        orig_data: read_cam 返回的原始数据
        replacements: {(section_ext, file_name): [{id, text}]} 需要替换的文本
    
    返回: bytearray (新的 CAM 文件数据)
    """
    sections = orig_data['sections']
    
    # 收集所有文件（按原始偏移排序，保持数据顺序）
    all_files = []
    for sec in sections:
        for f in sec['files']:
            all_files.append({
                'section': sec['ext'],
                'name': f['name'],
                'offset': f['offset'],
                'size': f['size'],
                'raw': f['raw'],
            })
    all_files.sort(key=lambda x: x['offset'] if x['offset'] > 0 else 0xFFFFFFFF)
    
    # 计算文件布局:
    # [header(20)] [section表(8*N)] [section数据(8+28*files_per_section) * N] [文件数据区]
    header_end = 20 + len(sections) * 8
    section_blob_size = sum(8 + 28 * len(sec['files']) for sec in sections)
    data_start = header_end + section_blob_size
    
    # 构建新的文件数据区
    new_data_region = bytearray()
    file_map = {}  # (section, name) -> (new_offset, new_size)
    
    for f in all_files:
        key = (f['section'], f['name'])
        if key in replacements:
            new_raw = replacements[key]
        else:
            new_raw = f['raw']
        
        new_offset = data_start + len(new_data_region)
        new_data_region.extend(new_raw)
        file_map[key] = (new_offset, len(new_raw))
    
    # 构建 section 数据区: 每个 section 的 header(8字节) + 该 section 的文件条目(每个28字节)
    section_blob = bytearray()
    new_section_offsets = []
    
    for sec in sections:
        sec_offset = header_end + len(section_blob)
        new_section_offsets.append(sec_offset)
        
        num_files = len(sec['files'])
        section_blob.extend(struct.pack('<I', num_files))
        section_blob.extend(struct.pack('<I', 0))  # dummy
        
        for f in sec['files']:
            key = (sec['ext'], f['name'])
            new_offset, new_size = file_map.get(key, (f['offset'], f['size']))
            
            name_bytes = f['name'].encode('ascii').ljust(20, b'\x00')[:20]
            section_blob.extend(name_bytes)
            section_blob.extend(struct.pack('<I', new_offset))
            section_blob.extend(struct.pack('<I', new_size))
    
    # 组装新文件
    new_file = bytearray()
    new_file.extend(orig_data['data'][:12])  # CYLBPC + padding
    new_file.extend(struct.pack('<I', len(sections)))  # num_sections
    orig_dummy = struct.unpack_from('<I', orig_data['data'], 16)[0]
    new_file.extend(struct.pack('<I', orig_dummy))  # dummy (保留原值)
    
    # Section 表
    for i, sec in enumerate(sections):
        new_file.extend(sec['ext_raw'])
        new_file.extend(struct.pack('<I', new_section_offsets[i]))
    
    # Section 数据 (header + entries 混合)
    new_file.extend(section_blob)
    
    # 文件数据区
    assert len(new_file) == data_start, f"Layout mismatch: {len(new_file)} != {data_start}"
    new_file.extend(new_data_region)
    
    return bytes(new_file)


if __name__ == '__main__':
    # 测试: 读取 HD textdata.cam, 解析 APMK, 重新构建, 对比
    import sys
    sys.path.insert(0, os.path.dirname(__file__))
    
    cam_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\Data\textdata.cam'
    
    print("=== Reading CAM ===")
    cam = read_cam(cam_path)
    print(f"Sections: {len(cam['sections'])}")
    for sec in cam['sections']:
        print(f"  {sec['ext']}: {len(sec['files'])} files")
    
    # 测试解析 STRT
    for sec in cam['sections']:
        if sec['ext'] == 'STRT':
            for f in sec['files']:
                if f['name'] == 'APMK':
                    strings = parse_strt(f['raw'])
                    print(f"\nAPMK: {len(strings)} strings")
                    for s in strings[:5]:
                        print(f"  [{s['id']}] {s['text']}")
                    
                    # 测试重建
                    new_raw = build_strt(strings, force_utf16=False)
                    print(f"\nRebuild: orig_size={len(f['raw'])}, new_size={len(new_raw)}")
                    
                    # 验证: 重建后能否解析出相同数据
                    new_strings = parse_strt(new_raw)
                    print(f"Re-parsed: {len(new_strings)} strings")
                    match = True
                    for i in range(min(len(strings), len(new_strings))):
                        if strings[i]['text'] != new_strings[i]['text']:
                            print(f"  MISMATCH at {i}: orig='{strings[i]['text']}' new='{new_strings[i]['text']}'")
                            match = False
                    if match:
                        print("  ✓ All strings match!")
                    break
            break
