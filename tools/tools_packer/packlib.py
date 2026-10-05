# -*- coding: utf-8 -*-
"""Majesty HD CAM 容器 解包/打包 核心库（字节级精确往返）。

CAM 格式（依据 FxRipper CAM_Header.pas + 实测验证）::

    偏移 0:   Magic "CYLBPC" + 2 字节空格填充 (8 bytes)
    偏移 8:   Version (4 bytes, 实测 01 00 01 00)
    偏移 12:  num_sections (uint32 LE)
    偏移 16:  file_dummy (uint32 LE)  = 非FONT section header 大小之和
    偏移 20:  Section Table: num_sections x 8 bytes
                每条: ext(4 bytes ASCII) + offset(uint32 LE, 绝对偏移)
    每个 section (由 table offset 指向):
        file_count (uint32 LE)
        section_dummy (uint32 LE, 实测全零)
        file_count x entries:
            name (20 bytes, ASCII, null 填充)
            offset (uint32 LE, 绝对文件偏移)
            size (uint32 LE)
    数据区: 各文件数据按 section 顺序、section 内文件顺序连续存放

file_dummy 字段语义（实测 6 个 CAM 文件确认）::
    file_dummy = sum(8 + nfiles * 28) for each non-FONT section
    即 20 + num_sections*8 + file_dummy = 数据区起点 (无FONT时)
                                       或 FONT section 起点 (有FONT时)

设计要点:
    - 解包时保留所有原始字节 (magic, version, ext_raw, name_raw, dummies)
    - 打包时重算所有偏移，保证与原版逐字节相同
    - STRT section 额外导出 .json 供文本编辑，重打包仍以 .bin 为准
"""
import os
import struct
import json
import hashlib

MAGIC = b'CYLBPC  '          # 8 bytes (含 2 个空格)
HEADER_SIZE = 20              # magic(8) + version(4) + num_sections(4) + dummy(4)
SECTION_ENTRY_SIZE = 8        # ext(4) + offset(4)
FILE_ENTRY_SIZE = 28          # name(20) + offset(4) + size(4)
NAME_SIZE = 20


# ---------------------------------------------------------------------------
# SHA-256
# ---------------------------------------------------------------------------
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(1 << 20), b''):
            h.update(blk)
    return h.hexdigest()


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------------------
# 读取 / 解析
# ---------------------------------------------------------------------------
def read_cam(data):
    """解析 CAM 字节，返回结构 dict。

    返回::

        {
            'magic': bytes(8),
            'version': bytes(4),
            'num_sections': int,
            'file_dummy': int,
            'sections': [
                {
                    'ext': str,            # 4 字符 ASCII
                    'ext_raw': bytes(4),    # 原始 ext 字节
                    'offset': int,         # section 在文件中的绝对偏移
                    'file_count': int,
                    'section_dummy': int,
                    'files': [
                        {
                            'name': str,       # 去尾部 null 的名字
                            'name_raw': bytes(20),  # 原始 20 字节
                            'offset': int,     # 数据绝对偏移
                            'size': int,
                            'raw': bytes,     # 文件数据
                        }, ...
                    ],
                }, ...
            ],
        }
    """
    if data[0:6] != b'CYLBPC':
        raise ValueError('Not a CAM file (magic mismatch: %r)' % data[:8])

    magic = bytes(data[0:8])
    version = bytes(data[8:12])
    num_sections = struct.unpack_from('<I', data, 12)[0]
    file_dummy = struct.unpack_from('<I', data, 16)[0]

    # Section table
    sections = []
    pos = HEADER_SIZE
    for i in range(num_sections):
        ext_raw = bytes(data[pos:pos + 4])
        ext = ext_raw.decode('ascii', errors='replace').rstrip('\x00')
        offset = struct.unpack_from('<I', data, pos + 4)[0]
        sections.append({
            'ext': ext,
            'ext_raw': ext_raw,
            'offset': offset,
            'files': [],
        })
        pos += SECTION_ENTRY_SIZE

    # Parse each section
    for sec in sections:
        sec_off = sec['offset']
        file_count = struct.unpack_from('<I', data, sec_off)[0]
        section_dummy = struct.unpack_from('<I', data, sec_off + 4)[0]
        sec['file_count'] = file_count
        sec['section_dummy'] = section_dummy

        fpos = sec_off + 8
        for j in range(file_count):
            name_raw = bytes(data[fpos:fpos + NAME_SIZE])
            name = name_raw.decode('ascii', errors='replace').rstrip('\x00')
            file_offset = struct.unpack_from('<I', data, fpos + NAME_SIZE)[0]
            file_size = struct.unpack_from('<I', data, fpos + NAME_SIZE + 4)[0]
            raw = bytes(data[file_offset:file_offset + file_size])
            sec['files'].append({
                'name': name,
                'name_raw': name_raw,
                'offset': file_offset,
                'size': file_size,
                'raw': raw,
            })
            fpos += FILE_ENTRY_SIZE

    return {
        'magic': magic,
        'version': version,
        'num_sections': num_sections,
        'file_dummy': file_dummy,
        'sections': sections,
    }


# ---------------------------------------------------------------------------
# 构建 / 打包
# ---------------------------------------------------------------------------
def calc_file_dummy(sections):
    """计算 file_dummy = 非FONT section header 大小之和。

    每个 section header 大小 = 8 (file_count + dummy) + nfiles * 28。
    FONT section 不计入。
    """
    total = 0
    for sec in sections:
        if sec['ext'] != 'FONT':
            total += 8 + len(sec['files']) * FILE_ENTRY_SIZE
    return total


def write_cam(cam):
    """根据结构 dict 重建 CAM 字节。

    所有偏移重新计算，保证布局正确。
    file_dummy 优先使用 cam 中保存的原始值，若缺失则自动计算。
    """
    sections = cam['sections']
    num_sections = len(sections)

    # Section table + all section headers 的总大小
    section_table_size = num_sections * SECTION_ENTRY_SIZE
    section_headers_size = sum(
        8 + len(sec['files']) * FILE_ENTRY_SIZE for sec in sections
    )
    data_start = HEADER_SIZE + section_table_size + section_headers_size

    # 第一遍：计算每个 section 的新偏移
    sec_offsets = []
    cur = HEADER_SIZE + section_table_size
    for sec in sections:
        sec_offsets.append(cur)
        cur += 8 + len(sec['files']) * FILE_ENTRY_SIZE

    # 第二遍：计算每个文件的新数据偏移（按 section 顺序、文件顺序）
    file_new_offsets = []   # [[offset, size] per file per section]
    data_region = bytearray()
    for si, sec in enumerate(sections):
        sec_files = []
        for fi, f in enumerate(sec['files']):
            new_off = data_start + len(data_region)
            raw = f['raw']
            data_region.extend(raw)
            sec_files.append((new_off, len(raw)))
        file_new_offsets.append(sec_files)

    # 组装
    out = bytearray()
    out += cam.get('magic', MAGIC)
    out += cam.get('version', b'\x01\x00\x01\x00')
    out += struct.pack('<I', num_sections)

    # file_dummy: 优先用原始值，否则计算
    if 'file_dummy' in cam and cam['file_dummy'] is not None:
        out += struct.pack('<I', cam['file_dummy'])
    else:
        out += struct.pack('<I', calc_file_dummy(sections))

    # Section table
    for si, sec in enumerate(sections):
        out += sec['ext_raw']
        out += struct.pack('<I', sec_offsets[si])

    # Section headers + file entries
    for si, sec in enumerate(sections):
        out += struct.pack('<I', len(sec['files']))
        out += struct.pack('<I', sec.get('section_dummy', 0))
        for fi, f in enumerate(sec['files']):
            out += f['name_raw']
            new_off, new_size = file_new_offsets[si][fi]
            out += struct.pack('<I', new_off)
            out += struct.pack('<I', new_size)

    # Data region
    assert len(out) == data_start, \
        'Layout mismatch: %d != %d' % (len(out), data_start)
    out += data_region

    return bytes(out)


# ---------------------------------------------------------------------------
# STRT 解析（可选，用于导出 .json 供文本编辑）
# ---------------------------------------------------------------------------
def _find_utf16_null(raw, start):
    """在偶数偏移处搜索 UTF-16LE null terminator。"""
    pos = start if start % 2 == 0 else start + 1
    while pos + 1 < len(raw):
        if raw[pos] == 0 and raw[pos + 1] == 0:
            return pos
        pos += 2
    return -1


def parse_strt(raw):
    """解析 STRT 文件，返回 [{id, text}] 列表。"""
    if len(raw) < 16:
        return []
    count = struct.unpack_from('<H', raw, 0)[0]
    flags = struct.unpack_from('<H', raw, 2)[0]
    is_utf16 = (flags & 0x08) != 0
    u1 = struct.unpack_from('<I', raw, 4)[0]

    num_offsets = (u1 - 16) // 4 if u1 > 16 and count > 3 else 0
    data_start = u1 if u1 > 16 else 16

    offset_table = []
    for i in range(num_offsets):
        offset_table.append(struct.unpack_from('<I', raw, 16 + i * 4)[0])

    strings = []

    def read_cstr(pos):
        end = raw.find(b'\x00', pos)
        if end == -1:
            end = len(raw)
        return raw[pos:end].decode('ascii', errors='replace'), end

    def read_utf16(pos):
        if pos + 2 <= len(raw) and raw[pos:pos + 2] == b'\xff\xfe':
            pos += 2
        end = _find_utf16_null(raw, pos)
        if end == -1:
            end = len(raw)
        if end % 2 == 1:
            end += 1
        return raw[pos:end].decode('utf-16-le', errors='replace'), end

    if is_utf16:
        if num_offsets == 0:
            pos = 16
            if count >= 3:
                for i in range(count):
                    if pos + 4 > len(raw):
                        break
                    sid = struct.unpack_from('<I', raw, pos)[0]
                    pos += 4
                    text, end = read_utf16(pos)
                    strings.append({'id': sid, 'text': text})
                    pos = end + 2
            else:
                text, end = read_utf16(pos)
                strings.append({'id': 0, 'text': text})
                pos = end + 2
                while pos + 4 < len(raw) and len(strings) < count:
                    sid = struct.unpack_from('<I', raw, pos)[0]
                    pos += 4
                    text, end = read_utf16(pos)
                    strings.append({'id': sid, 'text': text})
                    pos = end + 2
        else:
            pos = data_start
            for i in range(min(3, count)):
                if pos + 4 > len(raw):
                    break
                sid = struct.unpack_from('<I', raw, pos)[0]
                pos += 4
                text, end = read_utf16(pos)
                strings.append({'id': sid, 'text': text})
                pos = end + 2
            for off in offset_table:
                if off + 4 > len(raw):
                    continue
                sid = struct.unpack_from('<I', raw, off)[0]
                text, end = read_utf16(off + 4)
                strings.append({'id': sid, 'text': text})
    else:
        if num_offsets == 0:
            pos = 16
            if count >= 3:
                for i in range(count):
                    if pos + 4 > len(raw):
                        break
                    sid = struct.unpack_from('<I', raw, pos)[0]
                    pos += 4
                    text, end = read_cstr(pos)
                    strings.append({'id': sid, 'text': text})
                    pos = end + 1
            else:
                text, end = read_cstr(pos)
                strings.append({'id': 0, 'text': text})
                pos = end + 1
                while pos + 4 < len(raw) and len(strings) < count:
                    sid = struct.unpack_from('<I', raw, pos)[0]
                    pos += 4
                    text, end = read_cstr(pos)
                    strings.append({'id': sid, 'text': text})
                    pos = end + 1
        else:
            pos = data_start
            for i in range(min(3, count)):
                if pos + 4 > len(raw):
                    break
                sid = struct.unpack_from('<I', raw, pos)[0]
                pos += 4
                text, end = read_cstr(pos)
                strings.append({'id': sid, 'text': text})
                pos = end + 1
            for off in offset_table:
                if off + 4 > len(raw):
                    continue
                sid = struct.unpack_from('<I', raw, off)[0]
                text, end = read_cstr(off + 4)
                strings.append({'id': sid, 'text': text})

    return strings


def build_strt(strings, force_utf16=True):
    """根据字符串列表构建 STRT 文件二进制数据。"""
    count = len(strings)
    flags = 0x0208 if force_utf16 else 0x0200
    is_utf16 = force_utf16

    def encode_text(text):
        if is_utf16:
            return b'\xff\xfe' + text.encode('utf-16-le') + b'\x00\x00'
        else:
            return text.encode('ascii', errors='replace') + b'\x00'

    def encode_entry(sid, text):
        return struct.pack('<I', sid) + encode_text(text)

    if count < 3:
        first_data = encode_text(strings[0]['text'])
        entry_datas = [encode_entry(s['id'], s['text']) for s in strings[1:]]
        data = first_data + b''.join(entry_datas)
        u1 = count * 4 + 4
        u2 = 16 + len(first_data) if count >= 2 else 0
        u3 = u2 + len(entry_datas[0]) if count >= 3 and entry_datas else 0
        header = struct.pack('<HHI', count, flags, u1) + struct.pack('<II', u2, u3)
        return header + data
    elif count == 3:
        all_entries = [encode_entry(s['id'], s['text']) for s in strings]
        data = b''.join(all_entries)
        u1 = 16
        u2 = 16 + len(all_entries[0])
        u3 = u2 + len(all_entries[1])
        header = struct.pack('<HHI', count, flags, u1) + struct.pack('<II', u2, u3)
        return header + data
    else:
        first_entries = [encode_entry(strings[i]['id'], strings[i]['text']) for i in range(3)]
        later_entries = [encode_entry(strings[i]['id'], strings[i]['text']) for i in range(3, count)]
        num_offsets = count - 3
        data_start = 16 + num_offsets * 4
        u2 = data_start + len(first_entries[0]) if count >= 2 else 0
        u3 = u2 + len(first_entries[1]) if count >= 3 else 0
        offset_table = []
        cur = data_start + sum(len(e) for e in first_entries)
        for e in later_entries:
            offset_table.append(cur)
            cur += len(e)
        offset_data = b''.join(struct.pack('<I', o) for o in offset_table)
        u1 = data_start
        header = struct.pack('<HHI', count, flags, u1) + struct.pack('<II', u2, u3)
        return header + offset_data + b''.join(first_entries) + b''.join(later_entries)


# ---------------------------------------------------------------------------
# 上层 API：解包到目录 / 从目录打包
# ---------------------------------------------------------------------------
def _safe_slug(s):
    s = ''.join(c if (c.isalnum() or c in '-_') else '_' for c in s)
    return s if s else 'unnamed'


def unpack_to_dir(infile, outdir, export_strt_json=False):
    """解包 CAM 文件到 outdir。

    产出::

        outdir/manifest.json   — 完整结构元数据
        outdir/sections/<ext>/  — 按 section 分目录
            <NNN>_<name>.bin    — 文件原始字节
            <NNN>_<name>.json   — STRT 解析结果 (可选, export_strt_json=True)

    返回 manifest dict。
    """
    data = open(infile, 'rb').read()
    cam = read_cam(data)
    base = os.path.basename(infile)
    os.makedirs(outdir, exist_ok=True)

    manifest = {
        'source': base,
        'size': len(data),
        'sha256': sha256_file(infile),
        'magic': cam['magic'].hex(),
        'version': cam['version'].hex(),
        'file_dummy': cam['file_dummy'],
        'sections': [],
    }

    secs_dir = os.path.join(outdir, 'sections')
    os.makedirs(secs_dir, exist_ok=True)

    for si, sec in enumerate(cam['sections']):
        sec_dir = os.path.join(secs_dir, sec['ext'])
        os.makedirs(sec_dir, exist_ok=True)

        sec_meta = {
            'ext': sec['ext'],
            'ext_raw': sec['ext_raw'].hex(),
            'section_dummy': sec.get('section_dummy', 0),
            'files': [],
        }

        for fi, f in enumerate(sec['files']):
            slug = '%03d_%s' % (fi, _safe_slug(f['name']))
            fname = slug + '.bin'
            fpath = os.path.join(sec_dir, fname)
            with open(fpath, 'wb') as fp:
                fp.write(f['raw'])

            fmeta = {
                'index': fi,
                'name': f['name'],
                'name_raw': f['name_raw'].hex(),
                'file': fname,
                'size': f['size'],
                'orig_offset': f['offset'],
            }

            # 可选：导出 STRT 解析结果
            if export_strt_json and sec['ext'] == 'STRT':
                try:
                    strings = parse_strt(f['raw'])
                    if strings:
                        jpath = os.path.join(sec_dir, slug + '.json')
                        with open(jpath, 'w', encoding='utf-8') as fp:
                            json.dump(strings, fp, ensure_ascii=False, indent=2)
                        fmeta['strt_json'] = slug + '.json'
                        fmeta['strt_count'] = len(strings)
                except Exception:
                    pass

            sec_meta['files'].append(fmeta)

        manifest['sections'].append(sec_meta)

    with open(os.path.join(outdir, 'manifest.json'), 'w', encoding='utf-8') as fp:
        json.dump(manifest, fp, ensure_ascii=False, indent=2)

    return manifest


def pack_from_dir(outdir):
    """从 unpack_to_dir 生成的 outdir 重组 CAM 字节。

    严格依据 manifest.json 重建，不做任何文本/编码转换，保证字节级一致。
    """
    mpath = os.path.join(outdir, 'manifest.json')
    with open(mpath, 'r', encoding='utf-8') as fp:
        manifest = json.load(fp)

    cam = {
        'magic': bytes.fromhex(manifest['magic']),
        'version': bytes.fromhex(manifest['version']),
        'file_dummy': manifest.get('file_dummy'),
        'sections': [],
    }

    secs_dir = os.path.join(outdir, 'sections')

    for sec_meta in manifest['sections']:
        sec_dir = os.path.join(secs_dir, sec_meta['ext'])
        sec = {
            'ext': sec_meta['ext'],
            'ext_raw': bytes.fromhex(sec_meta['ext_raw']),
            'section_dummy': sec_meta.get('section_dummy', 0),
            'files': [],
        }
        for fmeta in sec_meta['files']:
            fpath = os.path.join(sec_dir, fmeta['file'])
            with open(fpath, 'rb') as fp:
                raw = fp.read()
            sec['files'].append({
                'name': fmeta['name'],
                'name_raw': bytes.fromhex(fmeta['name_raw']),
                'raw': raw,
            })
        cam['sections'].append(sec)

    return write_cam(cam)


def detect_type(data):
    """检测是否为 CAM 文件。"""
    return 'cam' if len(data) >= 8 and data[0:6] == b'CYLBPC' else 'blob'


if __name__ == '__main__':
    print('packlib self-test:')
    # 构造一个最小 CAM 测试往返
    test_cam = {
        'magic': MAGIC,
        'version': b'\x01\x00\x01\x00',
        'file_dummy': None,  # 自动计算
        'sections': [
            {
                'ext': 'STRT',
                'ext_raw': b'STRT',
                'section_dummy': 0,
                'files': [
                    {'name': 'TEST', 'name_raw': b'TEST' + b'\x00' * 16,
                     'raw': b'\x01\x00\x00\x08\x00\x00\x00\x00\xff\xfeA\x00\x00\x00'},
                ],
            },
        ],
    }
    buf = write_cam(test_cam)
    cam2 = read_cam(buf)
    ok = (cam2['magic'] == MAGIC and cam2['num_sections'] == 1
          and cam2['sections'][0]['ext'] == 'STRT'
          and cam2['sections'][0]['files'][0]['raw'] == test_cam['sections'][0]['files'][0]['raw']
          and cam2['sections'][0]['files'][0]['name'] == 'TEST')
    print('CAM roundtrip:', 'OK' if ok else 'FAIL')

    # file_dummy 验证
    expected_dummy = calc_file_dummy(test_cam['sections'])
    print('file_dummy: got=%d expected=%d' % (cam2['file_dummy'], expected_dummy))
    print('file_dummy match:', 'OK' if cam2['file_dummy'] == expected_dummy else 'FAIL')
