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
//   StrObj 的 ANSI 赋值函数 sub_627A80（1316 个调用者）。
//   在这里检测源串是否为多字节 UTF-8：
//     - 否  → 原样走原函数，零风险
//     - 是  → MultiByteToWideChar(CP_UTF8) 转成宽字符，构造 wide StrObj
//             （[+7] |= 1），之后引擎全链路自动走 wide 分支，绕过死循环
//
// 实证依据（x32dbg）：
//   StrObj 布局  [+0]=data  [+4]=length(字节数)  [+7]=flags(bit0=isWide)
//                [+8]=length2
//   死循环现场：StrObj 0x0D5387F8 → [+4]=12, [+7]=0(ANSI), data=UTF-8 "激活模组"
//   来源：output/Data/InterfaceStrings.xml 的 IDTXT_MODDING_ACTIVATE_MODS
// ---------------------------------------------------------------------------

#ifndef STRFIX_H
#define STRFIX_H

#include <windows.h>

// ---------------------------------------------------------------------------
// 目标地址（ImageBase = 0x400000，无 ASLR 版本）
// ---------------------------------------------------------------------------
#define ADDR_STR_ASSIGN_ANSI     0x00627A80u   // sub_627A80: StrObj = const char*  (__thiscall, ret 4)
#define ADDR_STR_ASSIGN_WIDE     0x00627AD0u   // sub_627AD0: StrObj = const wchar_t* (__thiscall, ret 8)
#define ADDR_STR_ALLOC           0x00627890u   // sub_627890: 缓冲分配器（区分 isWide，wide 会写 0xFEFF BOM）
#define ADDR_MEASURE_LOOP        0x0066CA70u   // sub_66CA70: 测宽循环（死循环点，仅用于诊断日志）

// StrObj 结构偏移
#define STROBJ_OFF_DATA          0x00   // char*/wchar_t* 数据指针
#define STROBJ_OFF_LEN           0x04   // 长度（ANSI=字节数；wide=字符数）
#define STROBJ_OFF_FLAGS         0x07   // 标志位，bit0 = isWide
#define STROBJ_OFF_LEN2          0x08   // 长度副本

#define STROBJ_FLAG_WIDE         0x01

// ---------------------------------------------------------------------------
// StrObj —— 引擎字符串对象。仅声明到我们关心的字段，其余留空。
// ---------------------------------------------------------------------------
struct StrObj
{
    void*  data;      // [+0]
    int    length;    // [+4] 低 24 位有效（引擎内部把高 8 位当别的用途，见下）
    char   _pad5[3];  // [+5..+7]
    unsigned char flags;  // [+7]
    int    length2;   // [+8]
};

#endif // STRFIX_H
