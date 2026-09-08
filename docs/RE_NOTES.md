---
AIGC:
  ContentProducer: '001191110102MAD55U9H0F10002'
  ContentPropagator: '001191110102MAD55U9H0F10002'
  Label: '1'
  ProduceID: 'a7125c6c-c81c-49d8-947a-a2dbc5844923'
  PropagateID: 'a7125c6c-c81c-49d8-947a-a2dbc5844923'
  ReservedCode1: '59e107d6-b71c-4b13-906c-35852ed95da8'
  ReservedCode2: '59e107d6-b71c-4b13-906c-35852ed95da8'
---

# Majesty HD 逆向工程笔记 (RE_NOTES)

> 基于 MajestyHD.exe (Steam 版) 的 IDA 逆向分析，用于驱动 ASI 汉化 DLL (MajestyI_Text_Fix) 的开发。
> IDA 数据库: `I:\SteamLibrary\steamapps\common\Majesty HD\MajestyHD.exe.i64` (session: `majesty_hd_exe`)
> 最后更新: 2026-09-08 (v25.1)

---

## 1. 关键地址速查表

| 地址 | 类型 | 名称 | 说明 |
|------|------|------|------|
| `0x0066E7B0` | func | `sub_66E7B0` | **文字绘制主函数** (Hook 入口)。`__thiscall`，参数 `(this, a2 offsetX, a3 offsetY, a4 device)`。入口字节 `6A FF` (push -1)。 |
| `0x007CA9E4` | dword | `dword_7CA9E4` | **主场景/文字表面** (渲染设备对象 CYOffportIMP 指针)。a4=0 时从此读取。 |
| `0x007CA9E8` | dword | `dword_7CA9E8` | 后备表面 (back surface)。sub_5D7720 的 arg1 通常为此值。 |
| `0x007C522C` | dword | `dword_7C522C` | 视口对象 (viewport)。 |
| `0x00741CAC` | vtable | `EXPECTED_VT` | CYOffportIMP 的虚表地址 (用于设备对象校验)。 |
| `0x007C12FC` | dword | `dword_7C12FC` | 脏矩形管理器 (dirty rect manager)。 |
| `0x007C5228` | dword | `dword_7C5228` | 当前图层索引 (layer index)。 |
| `0x00673FB0` | func | `sub_673FB0` | 提交脏矩形。`__thiscall(this=dirtyMgr, rect*, layerIdx)`。内部做可见性检查 + 屏幕→世界坐标转换。 |
| `0x005D7720` | func | `sub_5D7720` | 引擎合成/重绘上屏函数。`cdecl(arg1=device, arg2/3=rect*, arg4/5=0)`。调用者 `add esp, 0x14`。 |
| `0x00454500` | func | `sub_454500` | 主渲染循环。读取脏矩形列表并调用 sub_5D7720 重绘。 |
| `0x0066C7D0` | func | `sub_66C7D0` | **下一字符位置计算** (逐字节递增)。UTF-8 CJK 处理的关键函数 (Majesty HD 卡死根因所在)。 |
| `0x0066CA70` | func | `sub_66CA70` | 字符布局/度量计算。从指定位置开始解析字符。 |
| `0x0066CDA0` | func | `sub_66CDA0` | 多行文本行数/行高计算。调用 sub_66C7D0 遍历文本。 |
| `0x0066CE40` | func | `sub_66CE40` | 文本宽度计算。返回总宽度。 |
| `0x0066D8D0` | func | `sub_66D8D0` | 字体度量查询。返回指针，[+16]=某度量值。 |
| `0x0066FD0` | func | `sub_646FD0` (实际 `0x00646FD0`) | 字符度量获取 (宽度等)。 |
| `0x00646E60` | func | `sub_646E60` | **读背景色**。 |
| `0x00646E50` | func | `sub_646E50` | **写背景色**。 |
| `0x00646E70` | func | `sub_646E70` | **写前景色**。 |
| `0x00646E80` | func | `sub_646E80` | 读描边色(混合色)。 |
| `0x00646C30` | func | `sub_646C30` | 获取当前字体状态 (存入输出结构)。 |
| `0x00646D90` | func | `sub_646D90` | 设置字体属性 (参数1=style)。 |
| `0x00646DF0` | func | `sub_646DF0` | 设置字体大小 (参数=字号)。 |
| `0x00646DA0` | func | `sub_646DA0` | 设置前景色 (参数=color)。 |
| `0x00646DD0` | func | `sub_646DD0` | 设置背景色 (参数=color)。 |
| `0x00647140` | func | `sub_647140` | 文本渲染器初始化 (字体选择)。 |
| `0x00647020` | func | `sub_647020` | 设置光标位置 (x, y)。 |
| `0x00647420` | func | `sub_647420` | 绘制单个字符 (glyph blit)。参数 `(charCode, device)`。 |
| `0x006471F0` | func | `sub_6471F0` | 文本渲染器清理/收尾。 |
| `0x0066EB40` | — | 字符属性表 lookup | `sub_66EB40` 区域: 字符宽度/属性查表 ([v4+20]..[v4+21] 区间)。 |
| `0x006CB590` | func | `sub_6CB590` | Tab 字符宽度计算 (rich text 表)。 |
| `0x006CA800` | func | `sub_6CA800` | Tab 字符宽度计算 (plain text 表)。 |
| `0x006CC7D0` | func | `sub_6CC7D0` | 字符属性链表初始化。 |

---

## 2. 渲染设备对象 (CYOffportIMP) 内存布局

设备对象 = CYOffportIMP 类实例。`dword_7CA9E4` 是全局指针指向它。
虚表地址 = `0x00741CAC`。

### 2.1 字段偏移

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| +0 | uint32 | vtable | 虚表指针 (= 0x741CAC) |
| +4 | — | (未知) | — |
| +8 | — | (未知) | — |
| +12 | — | (未知) | — |
| +16 | uint32 | **pixelBuf** | 像素缓冲区基地址 (直写显存) |
| +20 | uint32 | **width** | 表面宽度 (像素) |
| +24 | uint32 | **height** | 表面高度 (像素) |
| +28 | uint32 | **bpp** | 位深度 (16/24/32) |
| +32 | uint32 | **stride** | 行跨度 (字节) |
| +36..68 | — | (其他字段) | — |
| +72 | uint32 | **clipL** | 裁剪矩形 left |
| +76 | uint32 | **clipT** | 裁剪矩形 top |
| +80 | uint32 | **clipR** | 裁剪矩形 right |
| +84 | uint32 | **clipB** | 裁剪矩形 bottom |

### 2.2 虚表布局 (vtable)

| 虚表偏移 | 索引 | 签名 | 说明 |
|----------|------|------|------|
| +248 (0xF8) | [62] | `void __thiscall(this=device, int left, int top, int right, int bottom, int color, int zero)` | **FillRect** — 半透明矩形填充。颜色格式与 bpp 一致 (16bpp=RGB565, 含 alpha 高位)。原版文字绘制前用此填充背景。 |

> **注意**: `zero` 参数在原版两处调用中恒为 0，用途未完全确认 (可能是 flags 或 blend mode)。

---

## 3. 文本对象 (this) 内存布局

`sub_66E7B0` 的 `this` 参数 (ECX) 指向文本对象。以下为 `this` 作为 `DWORD*` 的索引 (乘4得偏移):

| 索引 | 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| [0] | +0 | void* | strObj.data | 字符串数据指针 (指向 StrObj) |
| [1] | +4 | void* | strObj.altData | 备用字符串数据 (内联时用) |
| [2] | +8 | uint32 | strObj.meta | 元信息 (bit0=1 表示 wide string) |
| [3] | +12 | uint32 | **startX** | 文本区域左边界 (世界坐标) |
| [4] | +16 | uint32 | **startY** | 文本区域上边界 |
| [5] | +20 | uint32 | **endX** | 文本区域右边界 |
| [6] | +24 | uint32 | **endY** | 文本区域下边界 |
| [7] | +28 | uint8 | **flags** | 文本属性标志 |
| [8] | +32 | uint32 | scrollStart | 滚动起始 (flags&0x10 时用) |
| [9] | +36 | uint32 | scrollEnd | 滚动结束 |
| [10] | +40 | uint32 | fontId | 字体 ID (传给 sub_647140) |
| [11] | +44 | uint32 | richTextFlag | 0=plain text, 非0=rich text (影响 Tab 计算) |
| [12] | +48 | uint32 | fgColorUsed | 前景色 (传给 sub_646DA0) |
| [13] | +52 | uint32 | **bgFillcolor** | **背景填充色** (传给 vtable[62] 的 color 参数；也传给 sub_646DD0)。默认 0x80FFFFFF (半透明白) |
| [17] | +68 | void* | charAttrHead | 字符属性链表头 (用于富文本) |
| [20] | +80 | void* | charAttrEnd | 字符属性链表尾 |
| [21] | +84 | void* | charAttrIter | 字符属性迭代器 |
| [23] | +92 | uint32 | (未知) | 传给 sub_66CE40 的参数 |
| [25] | +100 | uint32 | fgColor | 前景色 (用于 DirectBlit) |
| [26] | +104 | uint32 | bgColor | 描边色 (用于 DirectBlit) |
| [97] | +388 | uint8 | fontCharset | 字符集 (传给 sub_647140) |

### 3.1 flags 位定义

| bit | 值 | 含义 |
|-----|-----|------|
| 0 | 0x01 | 水平居中 (centered) |
| 1 | 0x02 | 右对齐 (rightAlign) / **背景填充** (sub_66E7B0 中 flags&2 触发 vtable[62] 整体填充) |
| 2 | 0x04 | 描边 (outlined) |
| 3 | 0x08 | 垂直居中 (vCentered) |
| 4 | 0x10 | 滚动文本 (使用 [8]/[9] 范围) |
| 5 | 0x20 | 右对齐 (另一种, sub_647020 用 v19-v66) |
| 6 | 0x40 | 禁用字符属性着色 (flags&0x40==0 时用属性链表颜色) |

### 3.2 StrObj 字符串对象布局

StrObj 由 `this[0]` 指向 (非 NULL 时):

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| +0 | void* | data | 字符串数据指针 |
| +4 | uint24 | length | 字符长度 (低24位) |
| +7 | uint8 | isWide | bit0=1 → UTF-16LE, bit0=0 → ASCII/ANSI |

当 `this[0]` 为 NULL 时, 字符串内联在 this 中:
- `this[1]` = 数据指针
- `this[2]` == 1 → wide string, 否则 narrow

---

## 4. sub_66E7B0 文字绘制流程 (完整反编译分析)

```
sub_66E7B0(this, a2, a3, a4):   // __thiscall, a2/a3 = 世界坐标偏移, a4 = device(0→dword_7CA9E4)
{
    // 1. 空文本检查
    if (!this[0] && !this[1]) return 0;
    if (!this[10]) return 0;   // fontId 为空

    // 2. 初始化文本渲染器
    sub_647140(&renderState, this[10], this[97]);  // 字体选择
    v79 = 0;

    // 3. 描边模式
    if (flags & 4) {
        sub_646D90(1);       // 启用描边样式
        sub_646DF0(247);     // 描边字号
    }

    // 4. 设置颜色
    sub_646E70(this[25]);    // 写前景色
    sub_646E50(this[26]);    // 写背景色

    // 5. 确定渲染设备
    if (!a4) a4 = dword_7CA9E4;

    // 6. 获取字体度量
    sub_646C30(&fontMetrics);  // 存入 v60 结构

    // 7. 计算屏幕坐标
    startX = this[3] + a2;
    endX   = this[5] + a2;
    startY = this[4] + a3;
    endY   = this[6] + a3;
    flags  = this[7];

    // 8. 滚动范围
    if (flags & 0x10) { ... 设置 scrollStart/scrollEnd ... }

    // 9. ★ 整体背景填充 (flags & 2)
    if (flags & 2) {
        vtable[62](a4, startX, startY, endX, endY, this[13], 0);
        //                    ↑ 整个文字区域            ↑ bgFillColor
    }

    // 10. 设置逐字符颜色
    sub_646DA0(this[12]);    // 前景色
    sub_646DD0(this[13]);    // 背景色

    // 11. 计算文本宽度
    textWidth = sub_66CE40(this, this[23], endX - startX);

    // 12. 垂直对齐
    if (!(flags & 8)) {
        drawY = startY + fontMetrics.ascent;     // 顶部对齐
    } else {
        lineCount = sub_66CDA0(this, -1);         // 行数
        drawY = startY + (endY - lineCount * lineHeight - startY) / 2;
    }

    // 13. 逐字符循环
    sub_6CC7D0(&charAttr, textWidth);  // 初始化字符属性游标
    pos = 0;

    do {
        // 13a. 读取当前字符
        ch = ReadChar(this, pos);   // wide→uint16, narrow→uint8

        // 13b. 字符布局
        sub_66CA70(this, pos, textWidth, -1, &charW, &charX, nullptr);
        //                         ↑ 输出字符宽度、X偏移

        // 13c. 水平对齐
        if (flags & 1) {       // 居中
            sub_647020(startX + (textWidth - charX) / 2, drawY);
        } else if (flags & 0x20) {  // 右对齐
            sub_647020(startX + textWidth - charX, drawY);
        } else {                // 左对齐
            sub_647020(startX, drawY);
        }

        // 13d. 设置字符颜色 (从属性链表读取)
        bgFill = sub_646E60(&renderState);   // 读背景色
        ... 属性链表遍历, 设置颜色 ...

        // 13e. ★ 逐字符背景填充 + 字符绘制
        while (ch != 0 && col < charW) {
            if (pos >= attrEnd) { ... 属性链表步进 ... }

            if (ch == 9) {  // Tab
                tabW = richText ? sub_6CB590(x) : sub_6CA800(x);
            } else if (ch >= 0x20) {
                sub_646FD0(ch, &metrics);  // 获取字符宽度
            }

            // ★ 逐字符背景填充
            vtable[62](a4,
                charLeft,          // v59 (字符左边界)
                drawY - ascent - 1, // charTop
                charRight,         // charLeft + charWidth
                drawY + descent + 1, // charBottom
                bgFill,            // 背景色
                0);

            // 绘制字符
            if (ch == 9) { ... Tab 处理 ... }
            else {
                sub_647420(ch, a4);  // ★ 字符 blit
            }

            pos = sub_66C7D0(this, pos);  // ★ 下一字符位置 (UTF-8 断词!)
        }

        // 13f. 行末处理
        drawY += lineHeight;
    } while (drawY < endY);

    // 14. 清理
    v79 = -1;
    sub_6471F0(&renderState);
    return pos;
}
```

---

## 5. vtable[62] FillRect — 核心残影修复机制

### 5.1 原版行为

原版 `sub_66E7B0` 在画文字前有**两处**调用 `vtable[62]` (虚表偏移 248):

1. **整体背景填充** (`@0x66e8d6`): `flags & 2` 时, 用 `this[13]`(bgFillColor) 填充整个文字矩形 `(startX, startY, endX, endY)`。颜色默认 `0x80FFFFFF` (白色, alpha=0x80)。

2. **逐字符背景填充** (`@0x66ed08`): 每个字符绘制前, 用当前背景色填充单字符矩形 `(charLeft, charTop, charRight, charBottom)`。

### 5.2 残影根因

汉化 DLL 的 `DirectBlitText` 直接用 GDI 绘制 CJK 字形到 pixelBuf, **跳过了 vtable[62] 填充** → 旧字无人覆盖 → 移动单位残影。

### 5.3 v25 方案

在 `DirectBlitText` 画字前调用 `vtable[62]` 填充半透明背景:

```c
// 获取设备对象 (从 a4 或 dword_7CA9E4)
// 读 vtable: *(uint32_t*)device
// 读 FillRect 函数指针: *(uint32_t*)(vtable + 248)
// 调用: fillRect(device, startX, startY, endX, fillBottom, bgFillColor, 0)
```

- 背景色: `this[13]`, 默认 `0x80FFFFFF`
- 半透明背景每帧覆盖旧字 → 无残影
- 不需要 BG-LRU 像素缓存 → 无血条伪影

### 5.4 v25.1 移动残影修复

vtable[62] 只填充**当前位置**, 单位移动时旧位置残影无人覆盖。

解决: 维护 512 slot 的位置追踪表 `(thisPtr, textHash) → prevRect`。每次画字前:
1. 查表找上一帧位置
2. 位置变了 → 先用 vtable[62] 填充**旧位置** → 覆盖旧字
3. 填充新位置
4. 更新表

---

## 6. 脏矩形系统

### 6.1 全屏脏矩形 (v14 方案, 已弃用)

```c
// dirtyMgr = dword_7C12FC
// layerIdx = dword_7C5228
// sub_673FB0: __thiscall(dirtyMgr, rect*, layerIdx)
// rect = {0, 0, width, height}  // 全屏
```

引擎在 `sub_454500` 中读脏矩形队列, 调用 `sub_5D7720` 合成上屏。
队列布局: `[mgr + layerIdx*4 + 0x34]` = count, `[mgr + layerIdx*4 + 0x3c]` = rect 数组。

### 6.2 效果

v14 全屏脏矩形方案**无效**: 引擎在 DirectBlit 直写后才消费脏矩形, 合成覆盖的是已含旧字的画面 → 残影仍在。实际测试提交 delta 基本为 0 (被丢弃)。

---

## 7. BG-LRU 迭代历史 (v15~v24, 已弃用)

### 7.1 原理

DirectBlit 直写 front surface, 画字前把背景像素缓存到堆, 重绘时先写回旧背景(擦旧字)再存新背景。

### 7.2 失败根因

BG-LRU 的根本矛盾: 文字像素画到 `clipB` (CJK 扩展后的底边), 血条在 `endY` 以下, 而 `clipB > endY` (CJK 字体比英文高)。

- 用 `clipB` 擦 → 碰血条 → 蓝色伪影
- 用 `endY` 擦 → 漏底部笔画 → moved 残影
- hit 和 moved 对擦除区域需求不同, 分开处理仍无法兼顾 → 陷入死循环

### 7.3 迭代速查

| 版本 | 核心改动 | 结果 |
|------|---------|------|
| v15 | basic bg cache + clip-limited writeback | 无效 (clip 限制擦不到旧字) |
| v16 | motion.log 采样确认 DirectBlit 每帧调用 | 推翻"拖影期间不调用"假设 |
| v17 | this+text 键, moved 立即擦, clip 去限制 | 伪影消但过度擦除(缺字闪烁) |
| v18 | this 失活过期 + drift 检测 | 大幅改善但误杀休眠字 |
| v19 | 移除时间过期, 仅 moved/hit 驱动 | 残影更重(moved drift 跳过擦除) |
| v20 | moved force=true + drift 70% | 文字无残影, 背景花块严重 |
| v21 | 恢复 drift(70%), moved 不 force | 文字✓, 血条伪影(pad 侵入) |
| v22 | pad ow+4→ow+2 | 血条伪影+字体消失 |
| v23 | 非对称pad(底=0) + hit-drift 不重Capture | **最佳**: 文字✓闪烁极少, 血条伪影 |
| v24 | hit擦endY / moved擦clipB | 文字✓不闪, 血条伪影仍重 → 弃用 BG-LRU |

---

## 8. 字符串读取

### 8.1 ReadFullString 实现逻辑

```c
// this[0] = StrObj 指针 (非空时)
// this[0] 为空时: this[1]=data, this[2]==1→wide
StrObj *obj = this[0];
if (obj) {
    isWide = obj[+7] & 1;
    length = obj[+4] & 0xFFFFFF;
    data   = *(void**)obj;   // obj[+0]
} else {
    isWide = (this[2] == 1);
    data   = this[1];
    length = 0;  // 需自行 strlen/wcslen
}
```

### 8.2 宽字符串编码

- wide string: UTF-16LE
- narrow string: ASCII / ANSI (单字节)

### 8.3 文本归一化

游戏传入的文本中 `\n` (0x0A) 需归一化为字面量 `[newline]` 以匹配词典 key。`\r` (0x0D) 删除。

---

## 9. 渲染管线 (DirectBlitText)

### 9.1 流程

1. `GetRenderDevInfo(a4, rdi)` — 获取设备对象 (pixelBuf, width, height, bpp, stride, clip)
2. 读取文本布局 (`this[3..7]`) + 偏移 (a2, a3) → 屏幕坐标
3. GDI 字体测量 (`GetGlyphOutlineW` + `GGO_METRICS`) → 字符宽度缓存
4. 自动换行计算 (CJK 任意断行, 空格断行)
5. 计算绘制位置 (居中/右对齐/垂直居中)
6. **v25: vtable[62] 填充半透明背景** (含 moved 旧位置填充)
7. 逐字符渲染:
   - `GetGlyphOutlineW` 获取字形位图 (GGO_BITMAP 1bpp 或 GGO_GRAY8 8bpp)
   - 描边: 向 8 方向偏移画背景色字形
   - 前景: 画前景色字形
   - 直接写入 pixelBuf (支持 8/16/24/32 bpp)

### 9.2 BPP 与颜色格式

| bpp | 格式 | 前景色示例 | 说明 |
|-----|------|-----------|------|
| 16 | RGB565 | 0xFFFF (白) | R5G6B5, 高位=红 |
| 24 | RGB888 | 0xFFFFFF | BGR 顺序 (低字节=B) |
| 32 | ARGB8888 | 0xFFFFFFFF | 含 alpha |
| 8 | 索引色 | 0xFF | 调色板 |

### 9.3 CJK 字体高度问题

原版 `endY = this[6] + a3` 基于拉丁字体行高。CJK 字体 `tmHeight` 更大, 多行文本会超出 `endY`。
解决方案: `effectiveEndY = startY + textHeight` (当 textHeight > endY-startY 时)。
这导致 `clipB = effectiveEndY > endY`, 与血条区域重叠 (BG-LRU 伪影根因, v25 已解决)。

---

## 10. 汉化字典系统

### 10.1 字典格式

- JSON: `{"english key": "中文译文", ...}`
- TXT: `english\t中文` (tab 分隔)
- 分片: `dict_0.json` ~ `dict_9.json`

### 10.2 词典查找流程

```
1. ReadFullString(this) → 英文文本 (UTF-8)
2. NormalizeText: \n → [newline], \r 删除
3. g_dict.find(normalizedText)
4. MISS → RollbackReplace (子串替换, 长短语优先)
5. 仍 MISS → 原版绘制
6. HIT → DirectBlitText (CJK 渲染)
```

### 10.3 Rollback 词汇表

格式: `{"english fragment": "中文片段", ...}`
按 key 长度降序排列, 大小写不敏感子串匹配。用于 MISS 后的局部替换 (如 UI 模板句中的局部译名)。

---

## 11. Hook 安装

### 11.1 MinHook

```c
MH_Initialize();
MH_CreateHook(0x66E7B0, &Hooked_66E7B0, &g_orig66E7B0);  // 文字绘制
MH_CreateHook(0x5D7720, &Hooked_5D7720, &g_orig5D7720);  // 引擎合成 (统计用)
MH_EnableHook(MH_ALL_HOOKS);
```

### 11.2 Hooked_66E7B0 逻辑

```c
int __fastcall Hooked_66E7B0(int ecx_this, int edx, int a2, int a3, int a4) {
    // 1. 读英文文本
    ReadFullString(ecx_this, enText);
    // 2. 归一化 + 查字典
    NormalizeText(enText);
    auto it = g_dict.find(enText);
    if (it == g_dict.end()) {
        // 3. MISS → rollback → 原版
        if (RollbackReplace(enText, replaced)) {
            DirectBlitText(...);  // 渲染替换后文本
        } else {
            g_orig66E7B0(...);  // 原版绘制
        }
    } else {
        // 4. HIT → DirectBlitText (CJK 渲染)
        DirectBlitText(ecx_this, a2, a3, a4, cnWstr, cnWlen);
    }
}
```

---

## 12. 工程信息

### 12.1 源码

- 工程: `G:\Projects\MajestyIExtend\MajestyI_Text_Fix\MajestyI_Text_Fix.vcxproj`
- 主文件: `dllmain.cpp` (~2200 行)
- 依赖: MinHook (`libMinHook.lib`)
- 编译: VS2017 v141_xp, Release|Win32 (x86)
- MSBuild 路径: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`

### 12.2 部署

- DLL 输出: `Release\MajestyI_Text_Fix.dll`
- 部署目录: `I:\SteamLibrary\steamapps\common\Majesty HD\scripts\`
- 部署名: `MajestyI_Text_Fix.asi` (ASI loader 加载)
- 配置: `MajestyI_TextFix.ini`
- 词典: `dict.json` / `dict_0.json` ~ `dict_9.json` / `dict.txt`
- 回退: `rollback.json`
- 日志: `MajestyI_TextFix.log`, `miss.log`, `hit.log`, `rollback.log`, `motion.log`
- 版本备份: `MajestyI_Text_Fix.vXXX.asi`

### 12.3 git

- 仓库: `G:\Projects\MajestyIExtend`
- 当前 HEAD: `192095b` (v25.1)

---

## 13. 已知问题与后续方向

### 13.1 v25.1 待验证

- [ ] 斜向移动残影是否消除 (moved 旧位置填充)
- [ ] moved fill 不会侵入血条 (旧位置无血条覆盖)
- [ ] 半透明蓝色背景的视觉可接受性

### 13.2 内核汉化方向

当前方案是 **Hook + 外部渲染** (GDI GetGlyphOutlineW → pixelBuf 直写)。后续内核汉化方向:

1. **引擎原生字体替换**: 替换引擎 fontId 指向的位图字体为 CJK 位图字体 (需要在 sub_647140 层面操作)
2. **引擎原生文本替换**: 在 ReadFullString 后替换字符串内容, 由引擎 sub_66E7B0 原生渲染 (需要兼容 UTF-8/CJK 的引擎字体)
3. **vtable[62] 优化**: 当前每帧调用两次 fillRect (moved+current), 可考虑仅在 moved 时调用
4. **背景色调优**: `dwordBase[13]` 的值可能因文本对象不同而变化, 需确认各场景下的实际值

### 13.3 关键约束

- Majesty HD 的 `sub_66C7D0` (下一字符位置) **不支持 UTF-8 CJK 断词** — 逐字节递增在 UTF-8 多字节字符中间断词返回错误位置, 导致后续 `sub_66CA70` 从错误位置解析 → continuation byte 被当 ASCII → 宽度溢出 → 死循环。这是不走引擎原生渲染的根因。
- `dword_7CA9E4` 在某些场景下可能为 0 (设备未初始化), 需要做 null check。
- 原版 `flags & 2` (背景填充) 不是所有文本对象都开启的 — UI 文字可能不设此 bit。汉化 DLL 的 vtable[62] 调用是**无条件**的, 可能对不需要背景填充的文本产生视觉差异。