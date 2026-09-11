// strfix.h
// ---------------------------------------------------------------------------
// Majesty HD 中文文本死循环修复 —— StrObj 收敛点 hook
//
// 目标：
//   HD 版把原版支持双字节(DBCS)的文本读取路径改坏了。汉化后的 XML/MQXML/CAM
//   里存的是 UTF-8 中文，被当成 ANSI(单字节) 字符串塞进引擎的 StrObj。
//   渲染前的「测宽循环」sub_66CA70 走 narrow 分支，按字节读、喂给宽度表，
//   多字节序列导致 ebx 累加溢出 → ebp 置 1 → 提前退出且 position 不前进
//   → 上层无进展 → 死循环（无响应）。
//
// 方案（收敛点思想）：
//   不去追那 7~8 个上游文本函数，而是 hook 它们共同的底层入口 ——
//   StrObj 的「字节区间赋值」函数 sub_627AD0(自身, const char* src, int count)。
//   （实测：XML 文本节点 <Text>激活模组</Text> 的值就是从这里进 StrObj 的。）
//   在这里检测源串是否为多字节 UTF-8：
//     - 否  → 原样走原函数，零风险
//     - 是  → MultiByteToWideChar(CP_UTF8) 转成宽字符，构造 wide StrObj
//             （[+7] |= 1），之后引擎全链路自动走 wide 分支，绕过死循环
//
// 实证依据（x32dbg，2026-09-10）：
//   StrObj 布局  [+0]=data  [+4]=24bit长度+[+7]=flags(bit0=isWide)  [+8]=length2
//   ★ 重要：[+4] 是一个 32bit 字段，低 24 位是长度，高 8 位([+7])是 flags。
//     写长度时必须只改低 24 位、保留高 8 位，否则会把 isWide 抹掉。
//     证据：sub_627890/子函数用 `and eax,0xFFFFFF` 屏蔽 flags 后再写长度；
//           sub_627C30(Free) 也用 `test eax,0xFFFFFF` 取长度。
//   死循环现场（v1.1 实证，2026-09-10 x32dbg）：
//     sub_66CDA0 的 wide 分支在 0x0066CDDE 读取 StrObj，该对象
//     [data]=堆垃圾(0xBAADF00D/0xFEEEFEEE 填充)、[+4]=0x0100000F(len=15,isWide=1)、
//     [+8]=4。宽扫描找不到 0x0000 终止符 → sub_66C7D0 不前进 → 死循环。
//     ★ 根因 = 「只分配、不填充内容」的路径留下未初始化缓冲（见 dllmain.cpp
//       v1.2 的 sub_627890 分配器清零）。
//   ★ 另注：sub_627890 的 wide 分支为 `malloc(2n+4)` 且 `data = malloc+2`，
//     0xFEFF BOM 写在 data-2 —— 因此正文起点就是 data（v1.3 修正）。
//   入口实测：sub_627AD0(self=ecx, src=[esp+4], count=[esp+8])  ← XML 文本在此落地
//   ★ 对照：sub_627A80(self, src) 是 strlen 版赋值，实测只处理纯 ASCII 配置串
//     （文件名/注册表键/XML 标签名…），中文文本从不经过它。故两者都要 hook。
// ---------------------------------------------------------------------------

#ifndef STRFIX_H
#define STRFIX_H

#include <windows.h>

// ---------------------------------------------------------------------------
// 目标地址表（★ v2.0：仅作**文档 / 诊断比对**用，不再参与任何 hook 地址计算！）
//
//   运行期地址由 sigscan.cpp 的 AOB 特征码扫描得到（Steam / GOG 自适应）。
//   本表的作用只有一个：日志里能一眼看出「本次运行的到底是哪一版」。
//
//     hook 目标                   Steam        GOG         两版偏移
//     ---------------------------------------------------------------------
//     sub_627A80 Assign(ANSI)     0x00627A80   0x0063C370   +0x118F0
//     sub_627AD0 AssignRange  ★   0x00627AD0   0x0063C3C0   +0x118F0
//     sub_627B10 AssignSub        0x00627B10   0x0063C400   +0x118F0
//     sub_627890 Alloc        ★★  0x00627890   0x0063C180   +0x118F0
//     sub_628240 NarrowAssign     0x00628240   0x0063CB30   +0x118F0
//     sub_66CDA0 MeasureLines ★★  0x0066CDA0   0x006816F0   +0x11950
//     sub_66CE40 MeasureWidth ★★  0x0066CE40   0x00681790   +0x11950
//     sub_4B4840 UiSetText        0x004B4840   0x004B5730   +0x000EF0
//     ---------------------------------------------------------------------
//   ★ 偏移量分多个簇（+0xEF0 / +0x118F0 / +0x11950 / +0x14300 / +0x14650），
//     **不存在统一偏移** —— 这正是必须改用特征码定位的原因。
//     （+0x14300 = DUNT 名槽 thunk 簇；两版映射见 dllmain.cpp 的 MX 名槽注释）
//
//   备查（未 hook 的函数，仅在逆向时定位用；同样由特征码派生或手工查证）：
//     sub_66CA70 测宽累加循环（死循环内层）  Steam 0x0066CA70 / GOG 0x006813C0
//     sub_66C7D0 下一字符位置               Steam 0x0066C7D0 / GOG 0x00681120
//     sub_627C30 StrObj 释放                Steam 0x00627C30 / GOG 0x0063C320
// ---------------------------------------------------------------------------
//
// 【★ v1.7：sub_628240 —— 「独一无二」显示成 "*r" 的根因】
//   反编译（IDA session 62413c7f，0x628240）：
//       _DWORD *__thiscall sub_628240(_DWORD *this, _BYTE *a2)
//       {
//         int v2 = 0;
//         if (a2 && *a2) { do ++v2; while (a2[v2]); }   // ★ 逐【字节】strlen
//         int v4 = *(this + 1);                          // 长度+flags 合并字段
//         int v5 = *(this + 1) & 0xFFFFFF;               // 当前长度
//         if (v2 != v5 || !v5 || (v4 & 0x1000000)) {
//             if (*this && v5) free((void*)(*this - ((v4 >> 23) & 2)));
//             *(this + 1) &= ~0x1000000u;                // ★ 显式清掉 wide 位
//             sub_627890((void**)this, v2);              // ★ 走 narrow 分配
//         } else if (!*this) { ... 同上 ... }
//         sub_6719F7(a2, *this, v2 + 1);                 // memcpy(data, src, v2+1)
//         *(this + 2) = v2;
//         return this;
//       }
//   它【永远】把结果存成窄串，且长度用逐字节 strlen —— 对 UTF-16LE 正文是致命的：
//       独一无二 = EC 72 | 00 4E | E0 65 | 8C 4E | 00 00
//                  ^独      ^一      ^无      ^二
//     因为「一 = U+4E00」的【低位字节就是 0x00】，strlen 数到第 3 字节就停：
//       v2 = 2  → 只存下 EC 72（1 个宽单元）+ NUL。
//   消费者侧自愈（SniffUtf16Le）要求「≥2 个宽单元」才敢判定为 UTF-16LE →
//   只剩 1 个单元时放弃 → 引擎按窄串渲染 EC 72：
//       0xEC 窄表查不到字形 → 画成 '*'，0x72 = 'r'  → 界面显示 "*r"
//
//   反证：默认/蛮力/禁用塔楼/巫师之战 的 UTF-16LE 字节里【没有 0x00】，
//   strlen 恰好等于 2×字符数 → 虽然也存成窄串，但自愈能识别并转宽 → 显示正常。
//   这正是「5 条多人对战模式里只有 独一无二 坏」的原因。
//
//   数据来源：DataMX/mx_btdata.cam §STRT/BTDN（BTD4=独一无二，flags=0x0208 UTF-16LE）
//   读取链路：sub_488BF0（枚举对战模式）→ sub_51FD70 → sub_6646F0(0,'BTDN',idx)
//             → sub_628240(self, *v7)
//   ★ 全 EXE 只有 0x51FE52 一处 push 'BTDN'（0x4E445442），即 sub_628240 是唯一消费点。
//
//   修法：在入口识别「UTF-16LE 且被 strlen 截断」的源串，直接按宽串存储
//   （复用 TryStoreUtf16Wide），与自愈对另外 4 条的处理结果保持一致。
#define STRFIX_628240_WIDE_FIX   1

// ★ v1.8：MX(DLC) 单元显示名运行时映射开关
//   DLC 单元名来自 mx_Unittype.cam 的 DUNT 内嵌 ASCII 名；
//   UNTN 是 DUNT 的伴生数组（下标=单元索引），不能插条目，故在赋值点做精确匹配替换。
//   置 0 可完全关闭（不动任何游戏数据，随时可退）。
#define STRFIX_MX_NAME_DICT      1

// ★ v1.11：EXE 内置 UI 字符串汉化开关（分辨率设置界面那几条）
//
//   这些串硬编码在 EXE 的 .rdata，既不在 CAM 也不在 XML，常规汉化手段够不到：
//     0x73bf84  "SCREEN RESOLUTION"           -> sub_4B4840(5030, s)  ★ 不经 StrObj
//     0x73d0ec  "Change Mode"                 -> sub_627A80(v3, s) @0x491fe7
//     0x73d0a8  "The DirectX 9 mode change..." -> sub_627A80(v3, s) @0x492017
//     0x73d114  "Resolution Changed"          -> sub_627A80(v5, s) @0x49264a
//     0x73d0f8  "Keep the new resolution?"    -> sub_627A80(v5, s) @0x49267a
//   （0x735f00 "-keepintroresolution" 是命令行开关，不能译）
//
//   译文对齐《王权 2》官方简中术语（DictRead_V7.txt）：
//     #UIOPTRESOLUTION          Screen Resolution -> 屏 幕 分 辨 率
//     #UIOPTSCRRESLTNCHNGE_TITLE Screen Resolution Change -> 更 改 屏 幕 分 辨 率
//     Windowed Mode -> 窗 口 模 式 ；change -> 更 改 ；restart -> 重 新 启 动
//   （2 代字间带空格是其字体渲染约定，1 代汉化不带空格，故去空格）
//
//   置 0 可完全关闭。
#define STRFIX_UI_TEXT_DICT      1

// sub_4B4840 / sub_66CDA0 / sub_66CE40 的地址 → 见本文件顶部「目标地址表」，
// 运行期一律由 sigscan.cpp 的特征码定位（v2.0 起不再有 ADDR_* 宏参与计算）。
//
// 【文本对象布局】（x32dbg + IDA 实证，sub_66CDA0 / sub_66CE40 同构）
//      int** this;
//      this[0]  (this+0x00) = StrObj*   主字符串（可能为 NULL）
//      this[1]  (this+0x04) = char*     内联短串
//      this[2]  (this+0x08) = int       ==1 时表示内联串按 wide 读
//      this[10] (this+0x28) = 有效性标志（为 0 直接返回 0）
//   读取循环：
//      v7 = this[0];
//      if (*((BYTE*)v7 + 7) & 1)  c = *(WORD*)(  *v7 + 2*idx);   // wide
//      else                       c = *(BYTE*)(  *v7 +   idx);   // narrow
//      if (!c) return;  idx = sub_66C7D0(idx);   // ← 不前进即死循环

// StrObj 结构偏移
#define STROBJ_OFF_DATA          0x00   // char*/wchar_t* 数据指针
#define STROBJ_OFF_LEN           0x04   // 长度（ANSI=字节数；wide=字符数）
#define STROBJ_OFF_FLAGS         0x07   // 标志位，bit0 = isWide
#define STROBJ_OFF_LEN2          0x08   // 长度副本

#define STROBJ_FLAG_WIDE         0x01

// ★ 严格 flags 规范：只允许 bit0 (isWide) 有效。
//   实测引擎大量 StrObj 的 [+7] 高字节是未初始化堆填充（29% 为 0xBA，
//   即 0xBAADF00D 首字节），一旦 bit0 碰巧为 1，引擎就按 UTF-16 解读
//   ANSI/垃圾缓冲 → 测宽死循环。故在分配点强制规范为 f & 0x01。
#define STROBJ_FLAG_MASK         0x01

// ---------------------------------------------------------------------------
// 字节区间赋值的源串最长我们愿意扫描/转换的长度上限（防御异常大 count）
// ---------------------------------------------------------------------------
#define STRFIX_MAX_SRC_BYTES   (1024 * 64)

// ---------------------------------------------------------------------------
// StrObj —— 引擎字符串对象。仅声明到我们关心的字段，其余留空。
//   ★ [+4] 与 [+7] 共用同一个 32bit 槽：低 24 位=长度，高 8 位=flags。
// ---------------------------------------------------------------------------
struct StrObj
{
    void*  data;      // [+0]
    int    length;    // [+4] 完整 32bit：低 24 位有效长度，高 8 位 = flags 字节
    int    length2;   // [+8]
};

// 取/设 24bit 长度（保留 flags 字节）
static inline int StrObj_GetLen(const StrObj* s)
{
    return (int)((unsigned)s->length & 0x00FFFFFFu);
}
static inline void StrObj_SetLen(StrObj* s, int n)
{
    s->length = (int)(((unsigned)s->length & 0xFF000000u) | ((unsigned)n & 0x00FFFFFFu));
}
// 取/设 flags（位于 [+7]，即 length 字段的最高字节）
static inline unsigned char StrObj_GetFlags(const StrObj* s)
{
    return (unsigned char)(((unsigned)s->length >> 24) & 0xFFu);
}
static inline void StrObj_SetFlags(StrObj* s, unsigned char f)
{
    s->length = (int)(((unsigned)s->length & 0x00FFFFFFu) | ((unsigned)f << 24));
}

#endif // STRFIX_H
