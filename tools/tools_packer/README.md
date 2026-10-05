# Majesty HD CAM 解包 / 打包 工具（tools_packer）

Python 3 工具，针对 Majesty HD（王权1高清版）的 CAM/UIData 容器做
**字节级精确**的解包与打包，并以原版资源做 SHA-256 往返验证。

## 支持的容器格式

所有 `CYLBPC` magic 的容器文件（`.cam` 和 `UIData*.dat`）：

| 来源 | 文件 | 说明 |
|------|------|------|
| HD update\Data | textdata.cam, gpltext.cam | 游戏主文本 |
| HD update\DataMX | mx_btdata.cam, mx_gpltext.cam, mx_rgstext.cam, mx_textdata.cam | 多人对战文本 |
| HD update\Data | UIData_*.dat (13个) | UI 菜单/文本（各分辨率版本） |
| M1 Data | *.cam (14个) | 原版资源（兼容） |

### CAM 格式结构

```
偏移 0:   Magic "CYLBPC  " (8 bytes, 含2空格)
偏移 8:   Version (4 bytes, 01 00 01 00)
偏移 12:  num_sections (uint32 LE)
偏移 16:  file_dummy (uint32 LE) = 非FONT section header 大小之和
偏移 20:  Section Table: num_sections × 8 bytes
            ext(4 bytes ASCII) + offset(uint32 LE)
每个 section:
    file_count (uint32 LE)
    section_dummy (uint32 LE, 全零)
    file_count × entries:
        name (20 bytes, null 填充) + offset(uint32 LE) + size(uint32 LE)
数据区: 文件内容按 section/文件顺序连续存放
```

`file_dummy` 字段语义（实测 19 个文件确认）：
`file_dummy = sum(8 + nfiles × 28)` 对所有非 FONT section。
即 `20 + num_sections×8 + file_dummy` = 数据区起点（无 FONT 时）
或 FONT section 起点（有 FONT 时）。

## 用法

### 解包
```bat
python unpack.py <输入.cam> [输出目录]
python unpack.py --dir <输入目录> <输出根目录>   :: 递归解包整棵目录树
:: 可选 --json 额外导出 STRT section 的 .json（解析后的字符串列表）
```

解包产物：
```
输出目录/
  manifest.json              — 完整结构元数据（magic/version/dummy/sections/files）
  sections/
    STRT/
      000_BTDN.bin           — 文件原始字节
      000_BTDN.json           — STRT 解析结果（--json 时生成）
    BTDT/
      000_BTD1.bin
      ...
    FONT/
      000_fnt2.bin
      ...
```

`manifest.json` 记录每个 section 的 `ext_raw`（4字节原始 ext）、
每个文件的 `name_raw`（20字节原始名称）和 `file_dummy`，
保证重打包时逐字节一致。

### 打包
```bat
python pack.py <解包目录> [输出.cam]
python pack.py --dir <解包根目录> <输出根目录>   :: 批量重建
```

严格依据 `manifest.json` 重建，不做任何文本/编码转换，与原版逐字节相同。

### 验证
```bat
python verify.py [--src <update根目录>] [--tmp <临时目录>]
```

遍历所有 `.cam` 和 `UIData*.dat` 文件，做 解包→重打包→SHA-256 比对，
输出 PASS/FAIL。

## 往返验证结果

| 来源 | 文件数 | 结果 |
|------|--------|------|
| HD update (6 .cam + 13 .dat) | 19 | 全部 PASS |
| M1 Data (12 个测试) | 12 | 全部 PASS（含 44MB interfacedata.cam） |

## 编辑流程

1. 解包：`python unpack.py mx_btdata.cam mx_btdata.unpacked --json`
2. 编辑 STRT 文本：修改 `sections/STRT/000_BTDN.json` 中的 `text` 字段
3. 重建 STRT bin：用 `packlib.build_strt()` 将 json 转回 .bin
4. 重新打包：`python pack.py mx_btdata.unpacked mx_btdata_new.cam`
5. 部署：覆盖游戏目录文件（必须直接覆盖，ASI Loader update 目录对 CAM 无效）

## 关键保证

- 原版资源往返 SHA-256 **完全一致**（19/19 通过）
- 保留所有原始字节：magic、version、ext、name、dummy 均原样保存
- 重打包时所有偏移重新计算，保证布局正确
- `file_dummy` 字段保留原始值；若结构变更（增删文件），自动重算为非FONT section header 大小之和

## 文件清单

| 文件 | 作用 |
|------|------|
| `packlib.py` | 核心库：read_cam / write_cam / unpack_to_dir / pack_from_dir / parse_strt / build_strt |
| `unpack.py` | CLI 解包工具 |
| `pack.py` | CLI 打包工具 |
| `verify.py` | 往返验证工具 |

## 格式逆向依据

- FxRipper `CAM_Header.pas`（Delphi 源码，`G:\Projects\Majesty1\unpacker_sourcecode\`）
- 实测 19 个 HD 文件 + 12 个 M1 文件确认字段语义
- `game-archive-localization` 技能的 `references/majesty-hd-cam-format.md`

> AI生成
