# Majesty HD 汉化项目（MajestyIExtend）

《Majesty HD》（王权1高清版）完整中文本地化方案。采用 **双层架构**：

1. **静态层** — CAM/UIData/XML 文件内嵌翻译（UTF-16LE + CJK 位图字体注入）
2. **动态层** — ASI DLL 运行时 hook 渲染函数，JSON 词典 + rollback 词汇替换，直接写入像素缓冲区渲染中文字形

---

## 目录结构

```
MajestyIExtend/
├── MajestyI_Text_Fix/          # ASI DLL 源码（动态渲染层）
│   ├── dllmain.cpp              # 主源码 (~1450 行)
│   ├── pch.h / pch.cpp          # 预编译头
│   ├── framework.h              # Windows 头文件
│   ├── MinHook/                 # MinHook 库（hook 框架）
│   ├── MajestyI_Text_Fix.vcxproj
│   └── MajestyI_Text_Fix.vcxproj.filters
│
├── tools/                      # Python 工具链（静态翻译层）
│   ├── cam_packer.py            # CAM 文件核心打包器
│   ├── translate_cam.py         # CAM STRT 文本翻译
│   ├── translate_uidata.py     # UIData 界面翻译
│   ├── translate_xml.py        # XML 任务文件翻译
│   ├── translate_btdata.py     # 多人战斗数据翻译
│   ├── inject_fonts.py         # CJK 位图字体注入
│   ├── build_font_cam.py       # 独立 FONT section 构建
│   ├── build_dict.py           # 外挂词典构建
│   ├── glossary.py             # 术语表（M2 标准）
│   ├── scan_quality.py          # 翻译质量扫描
│   ├── check_m1_terms.py       # M1 原版术语检查
│   ├── categorize_untranslated.py
│   ├── round3~5_translations.py # 分轮翻译脚本
│   ├── supplement_translate.py  # 补充翻译
│   ├── dump_untranslated.py     # 未翻译条目导出
│   ├── check_text.py            # 文本检查
│   ├── debug_match.py           # 匹配调试
│   └── data/                    # 翻译数据 (JSON/TXT)
│       ├── round3_translations.json
│       ├── round4_translations.json
│       ├── supplement_translations.json
│       ├── scan_need_translate.json
│       ├── scan_no_need_translate.json
│       ├── scan_untranslated.json
│       ├── untranslated.json
│       ├── untranslated_full.json
│       ├── untranslated_categorized.json
│       ├── scan_quality_issues.json
│       ├── scan_term_issues.json
│       └── xml_texts.json
│
├── deploy/                     # 部署产物（复制到游戏目录）
│   ├── scripts/                 # ASI DLL 运行时文件
│   │   ├── (MajestyI_Text_Fix.asi 编译后放置)
│   │   ├── (dict.json / dict_1.json / dict_2.json 词典)
│   │   ├── (rollback.json 词汇替换表)
│   │   ├── (MajestyI_TextFix.ini 配置)
│   │   └── (SourceHanSansHWSC-VF.ttf 字体)
│   ├── Data/                    # 游戏数据文件
│   │   ├── textdata.cam         # 主文本（UTF-16LE 汉化）
│   │   ├── gpltext.cam          # GPL 文本
│   │   ├── InterfaceStrings.xml
│   │   └── UIData_*.dat         # 界面数据（14 个分辨率）
│   ├── DataMX/                  # 资料片数据
│   │   ├── mx_textdata.cam
│   │   ├── mx_btdata.cam
│   │   ├── mx_gpltext.cam
│   │   └── mx_rgstext.cam
│   ├── Quests/                  # 原版任务 (22 个 .mqxml)
│   └── QuestsMX/               # 资料片任务 (14 个 .mqxml)
│
├── .temp/                      # 临时工作文件（构建脚本等）
│   ├── build_dict2.py
│   ├── convert_dict.py
│   ├── create_rollback.py
│   └── fix_rollback.py
│
├── Release/                    # 编译输出
│   └── MajestyI_Text_Fix.dll
│
└── MajestyIExtend.sln          # VS2017 解决方案
```

---

## 一、静态翻译层

### 1.1 CAM 文件格式

CAM 是 Majesty 的自定义归档格式，包含多个 section（STRT / FONT / GDBT 等）。

**STRT section 结构：**
```
Header (16 bytes):
  count(2)  + flags(2) + u1(4) + u2(4) + u3(4)
  
flags:
  0x0200 = ASCII 编码（原版）
  0x0208 = UTF-16LE 编码（汉化后）

无偏移表 (count <= 3):
  [text+null] [ID(4)+text+null] ...

有偏移表 (count > 3, u1 > 16):
  偏移表: (count-3) 个 4 字节条目, 从 byte 16 开始
  数据区从 u1 开始:
    前3个: [ID(4)+text+null] x 3
    后续: 由偏移表指向 [ID(4)+text+null]

UTF-16LE 变体: text 前有 FFFE BOM, null terminator 为 0000
```

### 1.2 核心工具：cam_packer.py

读取 → 解析结构 → 替换文本 → 写回。被所有翻译脚本依赖。

**关键陷阱：**
- UTF-16LE null terminator 对齐：`_find_utf16_null` 只在偶数偏移匹配 `\x00\x00`，避免误匹配（如 U+5F00 首字节 0x00 与前一字符尾字节 0x00 形成假 null）
- `u1/u2/u3` 头部字段不混入偏移表（MajestyTool 的 bug，不能替代本工具）
- 汉化时统一从 ASCII(0x0200) 转为 UTF-16LE(0x0208)

### 1.3 字体注入

M1 原版 `textdata.cam` 包含 5 个 CJK 位图字体 section（fn10/fn11/fnt2/fnt4/fnt7），HD 版没有。
通过 `inject_fonts.py` / `build_font_cam.py` 将 M1 字体注入 HD 版 CAM。

### 1.4 翻译执行

| 脚本 | 目标 | 输出 |
|------|------|------|
| `translate_cam.py` | 主文本 CAM | `output/Data/textdata.cam` |
| `translate_uidata.py` | 界面数据 DAT | `output/Data/UIData_*.dat` |
| `translate_xml.py` | 任务 XML | `output/Quests/*.mqxml` |
| `translate_btdata.py` | 多人战斗 CAM | `output/DataMX/mx_btdata.cam` |

**术语标准**：以 M2（王权2）译名为准，通过 `glossary.py` 统一。

### 1.5 部署方式

将 `deploy/` 下文件复制到游戏安装目录，覆盖原文件。须先关闭游戏再部署（文件锁定）。

```
游戏目录/
├── MajestyHD.exe
├── Data/           ← deploy/Data/
├── DataMX/         ← deploy/DataMX/
├── Quests/         ← deploy/Quests/
├── QuestsMX/       ← deploy/QuestsMX/
└── scripts/        ← deploy/scripts/ (ASI + 词典 + 字体 + INI)
```

---

## 二、动态渲染层（ASI DLL）

### 2.1 架构概述

通过 Ultimate ASI Loader（`winmm.dll`）加载 `scripts\*.asi`，hook 游戏文本绘制函数 `sub_66E7B0`，拦截英文文本 → 查词典替换为中文 → 用思源黑体直接渲染到游戏像素缓冲区。

```
游戏调用 sub_66E7B0(this, a2, a3, a4)
        ↓
   Hooked_66E7B0 拦截
        ↓
   读取 this 对象中的英文文本
        ↓
   NormalizeText: \n → [newline] 归一化
        ↓
   dict.json 精确匹配? ── HIT ──→ DirectBlitText 渲染
        ↓ MISS
   rollback.json 词汇替换? ── HIT ──→ DirectBlitText 渲染
        ↓ MISS
   miss.log 记录 → 调用原版函数
```

### 2.2 Hook 目标

| 地址 | 名称 | 作用 |
|------|------|------|
| `0x66E7B0` | `sub_66E7B0` | 文本绘制函数（MinHook inline hook） |
| `0x7CA9E4` | `dword_7CA9E4` | 默认渲染设备 CYOffportIMP 指针 |
| `0x741CAC` | — | CYOffportIMP vtable（校验用） |

Hook 函数签名：`int __fastcall Hooked_66E7B0(int ecx_this, int edx_unused, int a2, int a3, int a4)`

### 2.3 CYOffportIMP 渲染设备布局

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x10 (this[4]) | pixelBuf | 像素缓冲区基地址（malloc 分配，非 DirectDraw） |
| 0x14 (this[5]) | width | 宽度 |
| 0x18 (this[6]) | height | 高度 |
| 0x1C (this[7]) | bpp | 位深（=16） |
| 0x20 (this[8]) | stride | 行跨度（=2048） |
| 0x38 (this[14]) | colorFormat | 颜色格式索引 |
| 0x48-0x54 (this[18-21]) | clipL/T/R/B | 裁剪矩形 |

像素地址计算：`pixelBuf + y * stride + (x * bpp) / 8`

### 2.4 sub_66E7B0 的 this 对象布局

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x08 (this[2]) | stringMode | 1=UTF-16, 其他=ANSI |
| 0x0C (this[3]) | startX | X 起始偏移 |
| 0x10 (this[4]) | startY | Y 起始偏移 |
| 0x14 (this[5]) | endX | 右边界 X |
| 0x18 (this[6]) | endY | 底部边界 Y |
| 0x1C (this[7]) | flags | 渲染标志位 |
| 0x64 (this[25]) | fgColor | 前景色 |
| 0x68 (this[26]) | bgColor | 描边色 |

**flags 位定义：**
- bit0 = 水平居中
- bit1 = 右对齐
- bit2 = 描边
- bit3 = 垂直居中
- bit4 = 裁剪
- bit5 = 右对齐2
- bit6 = 颜色覆盖

### 2.5 文本翻译流程

#### 2.5.1 归一化（NormalizeText）

dict.json 的 key 中用字面量 `[newline]` 表示换行，游戏传入的是实际 `\n` (0x0A)。
`NormalizeText()` 在查 dict 前把 `\n → [newline]`、删除 `\r`，使游戏文本与 dict key 格式一致。

dict value 和 rollback value 中的 `[newline]` 在加载时通过 `ReplaceNewlineLiteral()` 转为实际 `\n`。

#### 2.5.2 三级匹配

**第一级 — dict.json 精确匹配：**
- 主 dict.json + 分片 dict_0.json ~ dict_9.json（遇到不存在的文件即停止）
- key = 归一化后的英文文本，value = UTF-16LE 中文文本
- 命中 → 输出 `hit.log` → DirectBlitText 渲染

**第二级 — rollback.json 词汇替换：**
- dict MISS 后执行
- `g_rollback` 按 key 长度**降序排列**，长短语优先匹配
- 对文本做**大小写不敏感**的子串扫描替换
- 匹配后用空格填充原区域，防止短词二次匹配已替换区域
- 同一词在文本中所有出现位置都会被替换（`while` 循环 + `pos` 递进）
- 命中 → 输出 `rollback.log` → ReplaceNewlineLiteral → DirectBlitText 渲染

**第三级 — miss.log：**
- 前两级均未命中 → 输出 `miss.log`（去重）→ 调用原版函数

#### 2.5.3 rollback 最长匹配优先机制

```cpp
// 加载时排序
std::sort(g_rollback.begin(), g_rollback.end(), RollbackSortByLengthDesc);
// RollbackSortByLengthDesc: a.first.size() > b.first.size()

// 替换时遍历（长→短）
for (const auto& kv : g_rollback) {
    while ((pos = lowerText.find(lowerFrom, pos)) != npos) {
        outUtf8.replace(pos, from.size(), to);     // 替换译文
        lowerText.replace(pos, lowerFrom.size(),   // 填充空格防二次匹配
                          std::string(to.size(), ' '));
        pos += to.size();
    }
}
```

### 2.6 像素渲染（DirectBlitText）

游戏不使用 DirectDraw Lock/Unlock，而是直接通过 malloc 分配的系统内存像素缓冲区渲染。

**渲染流程：**
1. `GetRenderDevInfo()` 读取 CYOffportIMP 设备信息（pixelBuf/width/height/bpp/stride/clip）
2. 读取 this 对象中的文本布局参数（startX/Y, endX/Y, flags）
3. 应用 INI 配置的 XOffset/YOffset 偏移
4. `GetGlyphOutlineW()` 逐字测量宽度 + 自动换行
5. 计算绘制位置（居中/右对齐/垂直居中）
6. 计算裁剪矩形（clipL/T/R/B）
7. `GetGlyphOutlineW()` 逐字渲染字形到位图缓冲
8. 逐像素写入游戏像素缓冲区（支持 8/16/24/32bpp）

**Y 位置公式：**
```cpp
glyphTopY = curY + g_tmAscent - originY;
// 基线 = curY + tmAscent
// 字形顶部 = 基线 - originY
```

**clipB 裁剪修复（v12.7）：**
原版 `endY` 基于小字体行高（~12px），CJK 字体 tmHeight=17，多行文本超出 endY 被裁。
修复：当 `textHeight > (endY - startY)` 时，扩展 `effectiveEndY = startY + textHeight`，仍受 `rdi.clipB` 约束。

**渲染模式：**
- `GGO_BITMAP` (1bpp)：锐利无抗锯齿，直接写入
- `GGO_GRAY8` (8bpp)：抗锯齿，可选 alpha 混合

### 2.7 INI 配置

文件名：`MajestyI_TextFix.ini`，与 ASI 同目录。

```ini
[Font]
FontFile=SourceHanSansHWSC-VF.ttf
FontName=Source Han Sans HW SC VF
FontSize=12
FontWeight=400

[Render]
RenderMode=0      ; 0=GGO_BITMAP(锐利), 1=GGO_GRAY8(抗锯齿)
BlendMode=0       ; 0=直接写入, 1=alpha混合
Quality=0

[Color]
FgColor=65535     ; RGB565 (0xFFFF=白色)
BgColor=0
OverrideColor=1

[Position]
YOffset=-5        ; Y 偏移微调
XOffset=0
LineSpacing=0
WrapWidth=0       ; 0=自动用 endX-startX
WrapWidthAdjust=0

[Outline]
Enable=0
Width=1
```

### 2.8 词典文件格式

**dict.json / dict_N.json：**
```json
{
  "Build Guild Hall": "建造公会大厅",
  "Gold": "金币",
  "On Research:[newline] 100 gold": "研究支出:[newline] 100金币"
}
```
- key = 英文文本，`\n` 用字面量 `[newline]` 表示
- value = 中文译文，`[newline]` 在加载时转为实际 `\n`
- 支持分片：dict_0.json ~ dict_9.json 自动加载

**rollback.json：**
```json
{
  "Gold": "金币",
  " On Research:": " 研究支出:",
  "Temple to Agrela": "阿格雷拉神殿"
}
```
- key = 英文片段（不需要完整匹配，子串扫描）
- value = 中文替换
- 加载后按 key 长度降序排列

### 2.9 日志文件

| 文件 | 内容 |
|------|------|
| `MajestyI_TextFix.log` | 主日志（初始化、hook、统计） |
| `hit.log` | dict 精确匹配命中（去重） |
| `rollback.log` | rollback 词汇替换命中（去重） |
| `miss.log` | 未命中条目（去重） |

### 2.10 字体

- **主字体**：`SourceHanSansHWSC-VF.ttf`（思源黑体 HW SC 可变字体，~35MB）
- 通过 `AddFontResourceExA(FR_PRIVATE)` 私有加载
- GDI `GetGlyphOutlineW` 兼容
- tmHeight=17, tmAscent=14, tmDescent=3
- 有 SimSun 回退机制
- 通过 INI 的 `FontFile` 配置，与 ASI 同目录加载

---

## 三、编译与部署

### 3.1 编译

```powershell
# VS2017 + v141_xp 工具集, Release|x86
& "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\MSBuild\15.0\Bin\MSBuild.exe" `
    "G:\Projects\MajestyIExtend\MajestyIExtend.sln" `
    /p:Configuration=Release /p:Platform=x86 `
    /t:MajestyI_Text_Fix /v:minimal
```

**注意事项：**
- 平台名为 `x86`（非 `Win32`）
- 工具集 v141_xp（目标兼容 Windows XP）
- `dllmain.cpp` 必须有 **UTF-8 BOM**
- 输出：`Release\MajestyI_Text_Fix.dll`

### 3.2 部署

```powershell
# 编译产物 .dll → 游戏目录 .asi
Copy-Item "G:\Projects\MajestyIExtend\Release\MajestyI_Text_Fix.dll" `
    "I:\SteamLibrary\steamapps\common\Majesty HD\scripts\MajestyI_Text_Fix.asi" -Force
```

**部署清单（scripts/ 目录）：**
| 文件 | 说明 |
|------|------|
| `MajestyI_Text_Fix.asi` | 编译后的 ASI DLL |
| `MajestyI_TextFix.ini` | INI 配置 |
| `dict.json` | 主词典 |
| `dict_1.json` ~ `dict_2.json` | 分片词典 |
| `rollback.json` | 词汇替换表 |
| `SourceHanSansHWSC-VF.ttf` | 思源黑体字体 |

**ASI Loader 前置：**
游戏目录需有 `winmm.dll`（Ultimate ASI Loader），它会自动加载 `scripts\*.asi`。

### 3.3 验证

```powershell
# 确认 SHA1
Get-FileHash "I:\SteamLibrary\steamapps\common\Majesty HD\scripts\MajestyI_Text_Fix.asi" -Algorithm SHA1
```

启动游戏后检查日志：
- `MajestyI_TextFix.log` — 确认 hook 安装成功、词典加载条目数
- `hit.log` — 精确匹配统计
- `rollback.log` — 词汇替换统计
- `miss.log` — 未命中条目（用于补充词典）

---

## 四、版本演进

| 版本 | 方案 | 结果 |
|------|------|------|
| v11.0 | 直接像素缓冲区渲染 (GGO_BITMAP) | 中文首次成功显示 |
| v12.0 | INI 配置 + 自定义 TTF + Y 位置修复 | 字体问题解决 |
| v12.1 | 日志系统 + 自动换行 | 已部署 |
| v12.2 | JSON 词典 + WrapWidthAdjust | 已部署 |
| v12.3 | dict.json 格式统一 | 已部署 |
| v12.4 | rollback.json 词汇级兜底替换 | 已部署 |
| v12.5 | NormalizeText 归一化 + rollback 大小写不敏感 | 已部署 |
| v12.5.1 | dict 分片加载 (dict_0~9.json) | 已部署 |
| v12.6 | rollback 最长匹配优先 + dict_2.json + rollback 清理 | 已部署 |
| v12.6.1 | rollback 结果 [newline] → \n 修复 | 已部署 |
| v12.7 | clipB 裁剪修复（CJK 字体多行文本底部截断） | 已部署 |

---

## 五、已知问题与待办

1. **miss.log 收集**：大规模测试后根据 miss.log + rollback.log 继续补充 dict_3.json 等
2. **已翻译中文被 hook 捕获**：可能是静态层已翻译的中文文本再次进入 hook，需考虑跳过已是中文的文本
3. **rollback 优化**：继续添加安全长词组，删除有害短词
4. **屏幕溢出**：Cast 技能文本长度可能超出可用宽度，关注自动换行效果
5. **InterfaceStrings.xml**：纯 ASCII 不可翻译，否则卡死

---

## 六、关键技术备忘

### 6.1 编码处理

- **严禁用 PowerShell 处理含中文文本**（GBK 乱码 / UTF-8 BOM 问题），统一用 Python 读写并回读校验
- `dllmain.cpp` 必须有 UTF-8 BOM
- CAM 文件汉化时从 ASCII(0x0200) 统一转为 UTF-16LE(0x0208)

### 6.2 IDA MCP 注意事项

- session 易过期需重开
- `get_bytes` 对映像外地址静默返全 0
- 大 DLL 用 `run_auto_analysis=false`
- 反汇编不可靠时用 capstone 直读

### 6.3 CAM 格式陷阱

- `_find_utf16_null` 必须只在偶数偏移匹配，否则误截断 UTF-16LE 文本
- `u1/u2/u3` 是头部字段，不混入偏移表
- MajestyTool（C# 汉化辅助工具）的 STRLib.Read 有 bug（把 u1/u2/u3 混入偏移表），不能替代 cam_packer.py
- M1 与 HD 版 id 序列不一致，翻译时不能简单按 id 对应

### 6.4 渲染陷阱

- 游戏不使用 DirectDraw Lock/Unlock，直接通过 malloc 分配的系统内存渲染
- 原版 endY 基于小字体行高，CJK 字体更高，需要扩展 clipB
- YOffset 微调影响 endY（startY 和 endY 同时偏移），不能只调一个
- UTF-8 多字节字符在游戏原版逐字节递增逻辑下会断词，导致宽度计算错误和死循环

### 6.5 Git 工作流

- 勤用 git commit，每轮修复先 commit 再继续
- GitHub 推送需用户确认后才执行

---

## 七、工具链依赖

- **VS2017** (v141_xp, x86) — 编译 ASI DLL
- **Python 3** — 运行工具链脚本
- **MinHook** — inline hook 框架（已包含在项目中）
- **Ultimate ASI Loader** — ASI 加载器（`winmm.dll`）
- **IDA Pro + MCP** — 逆向分析（可选，用于地址定位）
- **SourceHanSansHWSC-VF.ttf** — 思源黑体 HW SC 可变字体