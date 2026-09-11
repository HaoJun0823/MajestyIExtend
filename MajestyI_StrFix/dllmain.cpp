// ===========================================================================
//  MajestyI_StrFix.dll
//  Majesty HD 中文文本死循环修复 —— StrObj 收敛点 Hook 方案
// ===========================================================================
//
//  【问题】
//  HD 版破坏了原版的 DBCS(双字节) 支持。汉化后的 XML / MQXML / CAM 中文本
//  是 UTF-8，被当作 ANSI(单字节) 字符串读入引擎的 StrObj。渲染前测量宽度时
//  sub_66CA70 走 narrow 分支逐字节读取，多字节序列把 ebx 累加溢出(置 ebp=1)
//  提前退出且 position 不前进 → 上层无进展 → 死循环(界面无响应)。
//
//  【方案：收敛点】
//  不追 7~8 个上游文本函数，只 hook 它们共同的底层入口：
//      sub_627A80 —— StrObj 的 strlen 版赋值（配置串/标签名，实测只走 ASCII）
//      sub_627AD0 —— StrObj 的「字节区间」赋值（★ 中文文本主路径）
//  在这里：
//    · 纯 ASCII  → 直接转发原函数（零行为改变）
//    · 含多字节 UTF-8 → MultiByteToWideChar(CP_UTF8) 转宽字符，
//      在 StrObj 内构造 wide 串（[+7] |= 1），之后引擎全链路自动走 wide 分支
//
//  【关键：复用引擎自己的分配器 sub_627890】
//  不自己 malloc 数据缓冲，而是先置 [+7] |= 1 再调用 sub_627890(字符数)，
//  让引擎按自己的规则分配（2*n+4，前两字节写 0xFEFF BOM）并回填 [+0]/[+4]，
//  这样长度语义、BOM、静态空串缓冲全部与引擎原生路径完全一致，破坏假设最少。
//
//  【StrObj 布局】（x32dbg 实证）
//      [+0] void*  data
//      [+4] 32bit 字段：低 24 位 = 长度(ANSI=字节数 / wide=字符数)，
//                       高 8 位 ([+7]) = flags（bit0 = isWide）
//      [+8] int    length2
//   ★ 写长度必须只改低 24 位，保留 flags 字节，否则会抹掉 isWide。
// ===========================================================================

#include "pch.h"
#include "config.h"
#include "strfix.h"
#include "MinHook.h"
#include <intrin.h>     // ★ v1.6：_AddressOfReturnAddress（记录调用方，用于反查生产者）

// ---------------------------------------------------------------------------
// 原函数指针
// ---------------------------------------------------------------------------
// 注意：这两个赋值函数都「返回 self（eax）」，调用方依赖它（例如
// 0x68D4CC 处会把 sub_627AD0 的返回值再当 src 传给 CopyFrom）。
// 因此原函数指针与 detour 都必须用 StrObj* 返回类型，否则 eax 是未定义值。
typedef StrObj* (__thiscall *AssignAnsiFn)(StrObj* self, const char* src);              // sub_627A80
typedef StrObj* (__thiscall *AssignRangeFn)(StrObj* self, const char* src, int count);  // sub_627AD0
typedef StrObj* (__thiscall *NarrowAssignFn)(StrObj* self, const char* src);            // sub_628240 ★ v1.7（逐字节 strlen 窄串赋值）
typedef int     (__thiscall *StrAllocFn)  (StrObj* self, int count);                    // sub_627890（★ eax = 新长度，必须原样回传）

// ★ v1.6 消费者侧自愈点（文本测量的两个唯一实现）
typedef int     (__thiscall *MeasureLinesFn)(int** thisptr, int a2);            // sub_66CDA0, ret 4 ★无迭代上限=死循环本体
typedef int     (__thiscall *MeasureWidthFn)(int** thisptr, int a2, int a3);    // sub_66CE40, ret 8

static AssignAnsiFn  g_orig_627A80 = nullptr;
static AssignRangeFn g_orig_627AD0 = nullptr;
static NarrowAssignFn g_orig_628240 = nullptr;  // ★ v1.7：逐字节 strlen 窄串赋值（BTDN 等表名走这里）
static StrAllocFn    g_orig_627890 = nullptr;   // ★ v1.2：分配器（根治点）

static MeasureLinesFn g_orig_66CDA0 = nullptr;  // ★ v1.6
static MeasureWidthFn g_orig_66CE40 = nullptr;  // ★ v1.6

#if HOOK_WIDE_ASSIGN_OBSERVE
typedef void (__thiscall *AssignSubObserveFn)(StrObj*, const char*, int, int, int, int);
static AssignSubObserveFn g_orig_627B10 = nullptr;
#endif

// ---------------------------------------------------------------------------
// 日志（★ 纯 Win32 API 实现，不依赖 CRT stdio）
//
// 为什么不用 fopen/fprintf：
//   本 DLL 是 v141_xp + /MD，走 UCRT；而游戏 EXE 是 VC90（msvcr90.dll）。
//   两个 CRT 在同一进程共存时，CRT stdio 的 FILE* 状态机 + setvbuf + 变参
//   格式化很容易踩到 UCRT 的内部断言（实测会走到 __fastfail 崩溃）。
//   改用 CreateFileA + WriteFile 后，日志路径完全不经过 CRT，
//   只保留 _vsnprintf_s 做「纯计算」的格式化（它是安全的有界函数）。
// ---------------------------------------------------------------------------
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_cs;      // 游戏可能多线程加载资源，日志要串行
static volatile LONG g_ready = 0;

// 统计
static volatile LONG g_n_total   = 0;  // 调用总数
static volatile LONG g_n_ascii   = 0;  // 纯 ASCII 转发
static volatile LONG g_n_utf8    = 0;  // 命中多字节并转换
static volatile LONG g_n_utf16   = 0;  // ★ v1.4：命中 UTF-16LE(CAM) 并原样转存
static volatile LONG g_n_sniff   = 0;  // ★ v1.5：靠内容形态嗅探命中的 UTF-16LE
static volatile LONG g_n_failed  = 0;  // 转换失败（回退原函数）
static volatile LONG g_n_zeroed  = 0;  // ★ v1.2：分配后清零次数
static volatile LONG g_n_layout_probe = 0;  // ★ v1.3：布局自检只打一次

// ★ v1.6：消费者侧自愈统计
static volatile LONG g_n_measure       = 0;  // 测量入口调用总数（仅诊断）
static volatile LONG g_n_heal          = 0;  // 自愈总次数
static volatile LONG g_n_heal_wide     = 0;  // A：wide 无终止符 → 强制空串
static volatile LONG g_n_heal_narrow   = 0;  // B/C/D1：narrow 形态不符 → 已转 wide
static volatile LONG g_n_heal_junk     = 0;  // D2：narrow 垃圾 → 强制空串
static volatile LONG g_n_trunc         = 0;  // ★ v1.7：sub_628240 处「被 strlen 截断的 UTF-16LE」命中次数
static volatile LONG g_n_mxdict        = 0;  // ★ v1.8：MX(DLC) 单元显示名运行时映射命中次数

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

// 把模块基址解析进来，并检查目标地址处是否是可读可执行的代码。
// 本项目 EXE 关闭了 ASLR（ImageBase 固定 0x400000），但为了稳健，
// 仍按「模块基址 + 固定 RVA」计算，并在日志里核对。
static HMODULE   g_exe_base  = nullptr;
static uintptr_t g_addr_627A80 = 0;
static uintptr_t g_addr_627890 = 0;
static uintptr_t g_addr_627AD0 = 0;
static uintptr_t g_addr_628240 = 0;   // ★ v1.7 截断修复点
static uintptr_t g_addr_4B4840 = 0;   // ★ v1.11 EXE 内置 UI 串（控件文本设置）
static uintptr_t g_addr_627B10 = 0;
static uintptr_t g_addr_66CDA0 = 0;   // ★ v1.6 消费者侧自愈点
static uintptr_t g_addr_66CE40 = 0;   // ★ v1.6 消费者侧自愈点

static void LogOpen(void)
{
    if (g_log != INVALID_HANDLE_VALUE) return;

    char path[MAX_PATH] = { 0 };
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&LogOpen, &self) && self)
    {
        GetModuleFileNameA(self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
    }
    strncat_s(path, MAX_PATH, LOG_FILE_NAME, _TRUNCATE);

    // 覆盖写。不经过 CRT，避免 FILE* 状态机问题。
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void LogClose(void)
{
    if (g_log != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(g_log);
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

static void LogRaw(const char* fmt, ...)
{
    if (g_log == INVALID_HANDLE_VALUE) return;

    // 1) 先做「纯计算」的格式化（有界，不碰文件系统）
    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    // 2) 拼时间戳前缀
    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1200];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "[%02d:%02d:%02d.%03d][T%05lu] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                (unsigned long)GetCurrentThreadId(), body);

    // 3) 直接写文件（唯一一处不是 CRT 的操作）
    EnterCriticalSection(&g_log_cs);
    DWORD written = 0;
    WriteFile(g_log, line, (DWORD)strlen(line), &written, nullptr);
    LeaveCriticalSection(&g_log_cs);
}

// 把字符串按「可打印 ASCII 显示 + 非 ASCII 用 \\xHH」的形式写进缓冲区
static void DumpBytes(const char* s, int nbytes, int maxbytes, char* out, size_t outsz)
{
    size_t o = 0;
    if (nbytes > maxbytes) nbytes = maxbytes;
    for (int i = 0; i < nbytes && o + 6 < outsz; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F)
        {
            out[o++] = (char)c;
        }
        else
        {
            o += (size_t)_snprintf_s(out + o, outsz - o, _TRUNCATE, "\\x%02X", c);
        }
    }
    if (o < outsz) out[o] = '\0';
}

// 纯十六进制 dump
static void DumpHex(const char* s, int nbytes, int maxbytes, char* out, size_t outsz)
{
    size_t o = 0;
    if (nbytes > maxbytes) nbytes = maxbytes;
    for (int i = 0; i < nbytes && o + 4 < outsz; ++i)
    {
        o += (size_t)_snprintf_s(out + o, outsz - o, _TRUNCATE, "%02X ",
                                 (unsigned char)s[i]);
    }
    if (o < outsz) out[o] = '\0';
}

// ---------------------------------------------------------------------------
// UTF-8 合法性判定
//
// 返回真表示「这个 ANSI 串里含有需要按 UTF-8 多字节解释的内容」。
// 严格校验：不只看首字节高位，还要验证后续字节都是 10xxxxxx，
// 并拒绝过长编码 / 代理区 / 超过 U+10FFFF，避免把真正的 Latin-1/CP1252
// 文本（如 é è ü）误判成 UTF-8。
// ---------------------------------------------------------------------------
static bool IsValidUtf8WithMultibyte(const char* s, int nbytes, int* out_bad_at)
{
    if (!s || nbytes <= 0) return false;
    if (out_bad_at) *out_bad_at = -1;

    bool has_multibyte = false;
    const unsigned char* p = (const unsigned char*)s;
    int total = nbytes;   // 用传入的字节数，避免依赖 strlen

    for (int i = 0; i < total; )
    {
        unsigned char c = p[i];

        if (c < 0x80) { ++i; continue; }

        int need;
        unsigned cp;
        if      ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07u; }
        else { if (out_bad_at) *out_bad_at = i; return false; }   // 10xxxxxx 开头或不合法首字节

        if (i + need >= total) { if (out_bad_at) *out_bad_at = i; return false; }  // 截断

        for (int k = 1; k <= need; ++k)
        {
            unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) { if (out_bad_at) *out_bad_at = i + k; return false; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }

        // 拒绝过长编码
        if (need == 1 && cp < 0x80u)      { if (out_bad_at) *out_bad_at = i; return false; }
        if (need == 2 && cp < 0x800u)     { if (out_bad_at) *out_bad_at = i; return false; }
        if (need == 3 && cp < 0x10000u)   { if (out_bad_at) *out_bad_at = i; return false; }
        // 拒绝 UTF-16 代理区 / 超范围
        if (cp >= 0xD800u && cp <= 0xDFFFu){ if (out_bad_at) *out_bad_at = i; return false; }
        if (cp > 0x10FFFFu)               { if (out_bad_at) *out_bad_at = i; return false; }

        has_multibyte = true;
        i += need + 1;
    }

    return has_multibyte;
}

// ---------------------------------------------------------------------------
// ★ v1.4：CAM 的 UTF-16LE 源串识别
//
//   安全读取 src 前 2 字节。src 通常指向堆缓冲或 .rdata 里的字符串字面量，
//   src-2 在两种情形下都可读；仅在「src 恰好位于已提交区起点且前一页未提交」
//   时会炸，用 SEH 兜住（本函数无 C++ 析构对象，可用 __try）。
// ---------------------------------------------------------------------------
static bool SafePeekU16Before(const char* src, unsigned short* out)
{
    __try
    {
        *out = *(const unsigned short*)(src - 2);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *out = 0;
        return false;
    }
}

// 返回：-1 = 不是 UTF-16LE 源
//        0 = BOM 紧邻 src 之前（CAM 记录形态），正文从 src 起
//        2 = src 自身就是 BOM，正文从 src+2 起
static int DetectUtf16LeSource(const char* src, int bytes)
{
#if STRFIX_DETECT_UTF16
    if (!src || bytes < 2) return -1;

    if (*(const unsigned short*)src == 0xFEFF) return 2;

    unsigned short prev = 0;
    if (SafePeekU16Before(src, &prev) && prev == 0xFEFF) return 0;

    return -1;
#else
    (void)src; (void)bytes;
    return -1;
#endif
}

// ---------------------------------------------------------------------------
// ★ v1.5：UTF-16LE 源串的「内容形态嗅探」
//
//   背景见 config.h / STRFIX_SNIFF_UTF16。
//   这里只回答一个问题：「这段字节按 UTF-16LE 解读，是不是一段像样的字符串？」
//
//   为什么值得做：CAM 的加载路径把 UTF-16LE 正文交给 ANSI 赋值函数时
//   【不带 BOM】，BOM 判定必然漏；而这段字节又一定不是合法 UTF-8
//   （汉字 UTF-16LE 的高字节 ≥0x80，且常落在 0x80..0xBF 的「续字节」区间
//   之外，或出现 0xA4 这种既非首字节也非续字节的孤字节）→ 被当 ASCII 转发。
//
//   安全边界（三重否决，宁可漏判也不误判）：
//     1) 长度必须偶数且 >= 4；出现内嵌 0x0000 直接否决
//     2) 每一个 16 位单元都必须是「合理可显示宽字符」，否则否决
//     3) 必须含 >=0x80 的字节（纯 ASCII 不会死循环，无需处理），
//        且 >=1/3 的单元落在 CJK/假名/谚文/全角区
// ---------------------------------------------------------------------------
static inline bool IsWideCharPlausible(unsigned short w)
{
    if (w < 0x20) return false;                     // 控制字符 → 不像正文
    if (w == 0xFFFD) return false;                  // 替换字符
    if (w >= 0x0020 && w <= 0x007E) return true;    // ASCII 可打印
    if (w >= 0x00A0 && w <= 0x024F) return true;    // 拉丁扩展 A/B
    if (w >= 0x0370 && w <= 0x03FF) return true;    // 希腊
    if (w >= 0x0400 && w <= 0x04FF) return true;    // 西里尔
    if (w >= 0x2000 && w <= 0x206F) return true;    // 通用标点（… — “ ” ‘ ’ •）
    if (w >= 0x20A0 && w <= 0x20CF) return true;    // 货币符号（€ ₽）
    if (w >= 0x2100 && w <= 0x214F) return true;    // 字母式符号（™ ℃ №）
    if (w >= 0x2190 && w <= 0x21FF) return true;    // 箭头
    if (w >= 0x2460 && w <= 0x24FF) return true;    // 带圈数字
    if (w >= 0x25A0 && w <= 0x25FF) return true;    // 几何图形（■ ▲）
    if (w >= 0x2600 && w <= 0x27BF) return true;    // 杂项符号/装饰
    if (w >= 0x2E80 && w <= 0x2EFF) return true;    // CJK 部首补充
    if (w >= 0x3000 && w <= 0x303F) return true;    // CJK 标点（。、「」）
    if (w >= 0x3040 && w <= 0x30FF) return true;    // 平假名/片假名
    if (w >= 0x3100 && w <= 0x312F) return true;    // 注音符号
    if (w >= 0x4E00 && w <= 0x9FFF) return true;    // CJK 统一表意（汉字主体）
    if (w >= 0xAC00 && w <= 0xD7AF) return true;    // 谚文音节
    if (w >= 0xF900 && w <= 0xFAFF) return true;    // CJK 兼容表意
    if (w >= 0xFE30 && w <= 0xFE4F) return true;    // CJK 兼容形式
    if (w >= 0xFF00 && w <= 0xFFEF) return true;    // 全角/半角形式
    return false;
}

// 返回：嗅探到的 wide 字符数；0 = 「不像 UTF-16LE 源」
static int SniffUtf16Le(const char* src, int bytes)
{
#if STRFIX_SNIFF_UTF16
    if (!src || bytes < 4 || (bytes & 1)) return 0;     // 至少 2 个单元，且偶数

    int n = bytes / 2;
    if (n > 4096) return 0;                             // 合理性上限

    bool hasHigh  = false;
    int  cjkCount = 0;

    for (int i = 0; i < n; ++i)
    {
        unsigned char lo = (unsigned char)src[2 * i];
        unsigned char hi = (unsigned char)src[2 * i + 1];
        if (lo >= 0x80 || hi >= 0x80) hasHigh = true;

        unsigned short w = (unsigned short)((unsigned)lo | ((unsigned)hi << 8));
        if (w == 0)                       return 0;     // 内嵌终止符 → 不是「整段正文」
        if (!IsWideCharPlausible(w))      return 0;     // 任一单元不合理 → 否决
        if (w >= 0x2E80)                  ++cjkCount;   // 非拉丁单元计数
    }

    if (!hasHigh)             return 0;                 // 纯 ASCII → 不处理
    if (cjkCount == 0)        return 0;                 // 一个 CJK 都没有 → 否决
    if (cjkCount * 3 < n)     return 0;                 // 少于 1/3 → 不像中文宽串

    return n;
#else
    (void)src; (void)bytes;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// ★ v1.2：清洗 flags 高字节里的「已知堆填充值」
//
// 实测（1.58MB 日志 / 16122 条 A80 pass）flags 高字节分布：
//     10441 × 0x00   正常
//      4737 × 0xBA   ← 0xBAADF00D（MSVC 未初始化堆填充）首字节，占 29%
//       429 × 0x10 / 391 × 0x02 / 57 × 0x44 / 43 × 0xFF ...
// 说明大量 StrObj 的 [+7] 从未被初始化，保留了 malloc 时的填充值。
// 一旦填充值 bit0 == 1（如 0xCD/0xDD/0xFD/0xAB/0x33/0xFF），引擎就会把
// ANSI/垃圾缓冲按 UTF-16 解读 → 宽扫描找不到终止符 → 死循环。
//
// 策略：只清「确定是堆填充」的字节值，保留 0x10 / 0x02 等可能合法的标志位。
// ---------------------------------------------------------------------------
static inline bool IsHeapFillByte(unsigned char f)
{
    switch (f)
    {
    case 0xBA:   // BAADF00D  —— "bad food"(未初始化堆)
    case 0xAB:   // ABABABAB  —— 未初始化堆
    case 0xFE:   // FEEEFEEE  —— HeapFree 后填充
    case 0xCD:   // CDCDCDCD  —— MSVC 未初始化(debug CRT)
    case 0xDD:   // DDDDDDDD  —— 已释放(dead)
    case 0xFD:   // FDFDFDFD  —— 栅栏(fence)
    case 0xCC:   // CCCCCCCC  —— 未初始化/对齐填充
        return true;
    default:
        return false;
    }
}

// 返回「清洗后的 flags」。若无需改动则原样返回。
static inline unsigned char SanitizeFlags(unsigned char f)
{
#if STRFIX_SANITIZE_FLAGS
    if (IsHeapFillByte(f))
        return 0;          // 已知堆填充 → 明确不是任何合法标志 → 归零
#endif
    return f;
}

// ---------------------------------------------------------------------------
// ★ 共享核心：把「一段 UTF-8 字节」写进 StrObj，并标记为 wide
//
//   前置：调用方已确认 src[0..srcBytes) 是含多字节的合法 UTF-8。
//   返回：true  = 已按 wide 写成功（调用方不要再走原函数）
//         false = 失败，调用方应回退原函数（保持零行为改变）
//
//   顺序很关键：先置 isWide，再调引擎分配器，让它走 wide 语义
//   （malloc(2n+4)，在 data-2 写 0xFEFF BOM，回填 [+0]/[+4]）。
// ---------------------------------------------------------------------------
static bool TryStoreAsWide(StrObj* self, const char* src, int srcBytes, const char* tag)
{
    // ★ v1.2：先清洗 flags —— 丢弃堆填充垃圾位，只保留真正合法的标志位。
    //   否则 (oldFlags | WIDE) 会把 0xBA 之类垃圾一起留下，对象状态不可预测。
    unsigned char rawFlags = StrObj_GetFlags(self);
    unsigned char oldFlags = SanitizeFlags(rawFlags);
    if (oldFlags != rawFlags)
        StrObj_SetFlags(self, oldFlags);   // 先把垃圾位清掉

    int           oldLen   = StrObj_GetLen(self);

    // 转成 UTF-16（先只问需要多少字符）
    int wchars = MultiByteToWideChar(CP_UTF8, 0, src, srcBytes, nullptr, 0);
    if (wchars <= 0)
    {
        InterlockedIncrement(&g_n_failed);
        LogRaw("!! [%s] UTF8->WIDE 计数失败 (err=%lu) self=%p len=%d flags=%02X → 回退原函数",
               tag, GetLastError(), self, srcBytes, oldFlags);
        return false;
    }

    StrObj_SetFlags(self, (unsigned char)(oldFlags | STROBJ_FLAG_WIDE));

    StrAllocFn engineAlloc = (StrAllocFn)g_addr_627890;
    engineAlloc(self, wchars);          // wide: malloc(2*n+4) + 写 0xFEFF BOM + 回填 [+0]/[+4]

    if (!self->data)
    {
        InterlockedIncrement(&g_n_failed);
        StrObj_SetFlags(self, oldFlags);
        StrObj_SetLen(self, oldLen);
        LogRaw("!! [%s] 引擎分配器返回空 data (wchars=%d) self=%p → 回退原函数", tag, wchars, self);
        return false;
    }

    // ★★★ v1.3 关键修正：正文必须从 data 开始写，不能 +2！
    //   sub_627890 的 wide 分支 = `malloc(2n+4)` → `data = malloc+2`，
    //   并把 0xFEFF BOM 写在 data-2（即 malloc 基址）。所以：
    //       [malloc]      = FF FE       ← BOM（分配器写的）
    //       [data = +2 ]  = 正文第 1 个 wchar  ← ★ 正是要写的起点
    //       [data+2n .. ] = 终止符 0x0000
    //       malloc 块总长 2n+4 = BOM(2) + 正文+NUL(2n+2)   ← 刚好装满
    //   引擎自己的宽串赋值 sub_6279E0 也是这个语义：
    //       mov ecx,[edi] / lea eax,[esi+esi+2] / push eax / push ecx / push ebx
    //       call sub_6719F7        ; sub_6719F7(src, dst = *this = data, 2n+2)
    //
    //   ★ 原代码误写 data+2 的后果（v1.2 崩溃实证）：
    //     正文整体右移 2 字节，且 dst[wchars]=0 落在 malloc(2n+4) 块末尾
    //     【之后 2 字节】→ 踩坏下一个堆块头 → ntdll 后续分配/释放时把我们的
    //     正文当指针解引用而 AV。崩溃转储证据：ntdll+0x51e5a 字节 = `8B 00`
    //     (mov eax,[eax])，eax = [base+4] = 0x6BBF0022，而 0x6BBF='殿'、
    //     0x0022='"' —— 正是最后写入的 `"殿下，我很高兴地向您报告…"`。
    wchar_t* dst = (wchar_t*)self->data;
    MultiByteToWideChar(CP_UTF8, 0, src, srcBytes, dst, wchars);

    // ★★★ v1.2：补写终止符！
    //   引擎自己的 wide 赋值（sub_6279E0）会拷贝 2*len+2 字节（含 NUL），
    //   sub_627AD0 会写 data[count]=0；唯独本 hook 此前漏写。
    //   malloc(2n+4) 的 +4 正是为 BOM(2) + 终止符(2) 预留：
    //   dst = data 时，dst[wchars] 恰好落在块的最后 2 字节（不多不少）。
    dst[wchars] = 0;

    // ★ 只用 24bit 长度，保留 flags 字节（否则会把刚置上的 isWide 抹掉）
    StrObj_SetLen(self, wchars);
    self->length2 = wchars;

    // ★ v1.3 一次性布局自检：证明 [data-2]=BOM、正文始于 data、终止符在块尾。
    //   期望：[data-2]=FF FE，[data+2n]=00 00，且 2n+2 恰好等于块可用大小。
    if (InterlockedIncrement(&g_n_layout_probe) == 1)
    {
        const unsigned char* b = (const unsigned char*)self->data;
        LogRaw("LAYOUT 自检 self=%p data=%p  [data-2]=%02X%02X(期望 FFFE BOM)"
               "  [data]=%02X%02X(首字符)  [data+2n]=%02X%02X(期望 0000 终止符)  n=%d",
               self, self->data, b[-2], b[-1], b[0], b[1],
               b[2 * wchars], b[2 * wchars + 1], wchars);
    }

#if LOG_LEVEL >= 2
    {
        char srcDump[1024];
        char wDump[1024];
#if LOG_DUMP_SOURCE
        DumpBytes(src, srcBytes, LOG_DUMP_MAX_BYTES, srcDump, sizeof(srcDump));
#else
        srcDump[0] = '\0';
#endif
        {
            size_t o = 0;
            for (int i = 0; i < wchars && o + 8 < sizeof(wDump); ++i)
                o += (size_t)_snprintf_s(wDump + o, sizeof(wDump) - o, _TRUNCATE,
                                         "%04X ", (unsigned)dst[i]);
            if (o < sizeof(wDump)) wDump[o] = '\0';
        }
        LogRaw("HIT  [%s] self=%p  ansi{len=%d flags=%02X} -> wide{n=%d}  data=%p  src=\"%s\"  w=[ %s]",
               tag, self, srcBytes, oldFlags, wchars, self->data, srcDump, wDump);
    }
#endif
    return true;
}

// ---------------------------------------------------------------------------
// ★ v1.4 核心：把「一段 UTF-16LE 正文」写进 StrObj，并标记为 wide
//
//   与 TryStoreAsWide 完全同构，只是源本身已是 UTF-16，无需再转码，直接搬。
//   前置：调用方已通过 DetectUtf16LeSource 确认来源。
//   顺序同样关键：先置 isWide → 调引擎分配器（走 wide 分支，写 BOM 到 data-2）
//                  → 正文从 data 起写 → 末尾补 0x0000 终止符。
//
//   为什么必须走引擎分配器而不能自己 malloc：
//     · wide 语义要求 data-2 处有 0xFEFF BOM（sub_627C30 Free 时靠它 data-=2）
//     · [+0]/[+4] 的回填规则与引擎完全一致，下游依赖
// ---------------------------------------------------------------------------
static bool TryStoreUtf16Wide(StrObj* self, const wchar_t* wsrc, int wchars, const char* tag)
{
    if (!self || !wsrc || wchars <= 0) return false;
    if (wchars > STRFIX_MAX_SRC_BYTES / 2) return false;

    // 同样先清洗 flags 垃圾位，再置 isWide
    unsigned char rawFlags = StrObj_GetFlags(self);
    unsigned char oldFlags = SanitizeFlags(rawFlags);
    if (oldFlags != rawFlags)
        StrObj_SetFlags(self, oldFlags);

    int oldLen = StrObj_GetLen(self);

    StrObj_SetFlags(self, (unsigned char)(oldFlags | STROBJ_FLAG_WIDE));

    StrAllocFn engineAlloc = (StrAllocFn)g_addr_627890;
    engineAlloc(self, wchars);       // wide: malloc(2n+4) + data-2 写 0xFEFF + 回填 [+0]/[+4]

    if (!self->data)
    {
        InterlockedIncrement(&g_n_failed);
        StrObj_SetFlags(self, oldFlags);
        StrObj_SetLen(self, oldLen);
        LogRaw("!! [%s] UTF16LE 路径分配失败 (wchars=%d) self=%p → 回退原函数", tag, wchars, self);
        return false;
    }

    // 正文起点 = data（BOM 在 data-2，由分配器写），末尾终止符落在块尾 2 字节
    wchar_t* dst = (wchar_t*)self->data;
    for (int i = 0; i < wchars; ++i) dst[i] = wsrc[i];
    dst[wchars] = 0;

    StrObj_SetLen(self, wchars);     // 只改低 24 位，保留 isWide
    self->length2 = wchars;

#if LOG_LEVEL >= 2
    {
        char wDump[1024];
        size_t o = 0;
        for (int i = 0; i < wchars && o + 8 < sizeof(wDump); ++i)
            o += (size_t)_snprintf_s(wDump + o, sizeof(wDump) - o, _TRUNCATE,
                                     "%04X ", (unsigned)dst[i]);
        if (o < sizeof(wDump)) wDump[o] = '\0';
        LogRaw("HITU [%s] self=%p  UTF16LE{bytes=%d} -> wide{n=%d}  data=%p  w=[ %s]",
               tag, self, wchars * 2, wchars, self->data, wDump);
    }
#endif
    return true;
}

// ===========================================================================
// ★★★★★ v1.7：sub_628240 —— 「被 strlen 截断的 UTF-16LE 正文」修复
// ===========================================================================
//
//  【问题】「独一无二」在多人对战模式列表里显示成 "*r"
//    见 strfix.h 顶部「目标地址表」/ sigscan.cpp 中 SIG_628240 的完整反编译与推演。
//    一句话：sub_628240 用【逐字节 strlen】给窄串定长，而 UTF-16LE 正文里
//    只要出现「低字节为 0x00」的汉字（一=U+4E00、刀=U+5200、匀=U+5300…），
//    strlen 就会在那个字节停下：
//        独一无二 = EC 72 | 00 4E | E0 65 | 8C 4E | 00 00
//                   ^独      ^一 ← 这里就是 0x00
//        → v2 = 2 → 只存下 EC 72 → 自愈因「不足 2 个宽单元」放弃
//        → 引擎按窄串渲染 0xEC(无字形→'*') + 0x72('r') = "*r"
//    而 默认/蛮力/禁用塔楼/巫师之战 的字节里没有 0x00 → strlen 结果本来就对，
//    自愈能正常把这些「窄容器装的 UTF-16 字节」转成宽串 → 显示正常。
//
//  【修法】在赋值入口识别这种「UTF-16LE 且已被 strlen 截断」的源串，
//    直接调用 TryStoreUtf16Wide 按宽串存储 —— 结果与自愈对另外 4 条的处理完全一致。
//
//  【触发条件】见 DetectTruncatedUtf16Le（全部满足才动手，宁可漏判不误判）。
//
//  【内存说明】sub_627890 只覆盖 *this 指针、不释放旧缓冲（原函数 sub_628240
//    在调用它之前自己 free）。本 hook 直接走 TryStoreUtf16Wide，未复刻那次 free；
//    目标对象通常是刚准备好、即将首次赋值的局部对象（旧长度=0/静态空串），
//    故实际不会泄漏；即便个别对象已有缓冲，泄漏量也只是几个短串，可忽略。
//    刻意【不】自行调用 CRT free —— 跨 CRT 释放引擎 malloc 的块会造成堆破坏。
// ---------------------------------------------------------------------------

// 返回：>0 = 嗅探到的宽字符数（*out_wsrc 为 UTF-16LE 正文起点）
//        0 = 「不是被截断的 UTF-16LE 正文」，调用方应走原函数
static int DetectTruncatedUtf16Le(const char* src, const wchar_t** out_wsrc)
{
#if STRFIX_628240_WIDE_FIX
    if (out_wsrc) *out_wsrc = nullptr;
    if (!src) return 0;

    __try
    {
        // 防御：源串自带 BOM 时跳过（当前 CAM 路径已剥掉 BOM，这里只是兜底）
        int off = 0;
        if ((unsigned char)src[0] == 0xFF && (unsigned char)src[1] == 0xFE)
            off = 2;

        const char* s = src + off;

        int L = 0;
        while (L < STRFIX_MAX_SRC_BYTES && s[L]) ++L;
        if (L <= 0 || L >= STRFIX_MAX_SRC_BYTES) return 0;
        if (L & 1) return 0;            // ★ 截断点必落在「低字节」→ 偏移必为偶数
        if (s[L + 1] == 0) return 0;    // ★ 宽串恰好到此结束 → 本来就没被截断

        int  n = 0, cjk = 0;
        bool hasHighLow = false;
        for (;;)
        {
            if (n >= 4096) return 0;
            unsigned char lo = (unsigned char)s[2 * n];
            unsigned char hi = (unsigned char)s[2 * n + 1];
            unsigned short w = (unsigned short)((unsigned)lo | ((unsigned)hi << 8));
            if (w == 0) break;                          // 宽终止符
            if (!IsWideCharPlausible(w)) return 0;      // 任一单元不合理 → 否决
            if (w >= 0x2E80) ++cjk;
            if (lo >= 0x80) hasHighLow = true;
            ++n;
        }

        if (n < 2) return 0;                   // 至少 2 个宽单元（自愈那关也是这么卡的）
        if (cjk == 0 || cjk * 3 < n) return 0; // 至少 1/3 非拉丁单元
        if (!hasHighLow) return 0;             // 排除纯 ASCII 误判
        if (2 * n <= L) return 0;              // 未发生截断 → 交回原函数

        {
            int k = L / 2;                     // 截断点所在的那个宽单元
            unsigned short wk = (unsigned short)(((unsigned char)s[2 * k]) |
                                                 ((unsigned char)s[2 * k + 1] << 8));
            if (wk < 0x2E80) return 0;         // 排除「Latin-1 窄串 + 尾部垃圾」误判
        }

        if (out_wsrc) *out_wsrc = (const wchar_t*)s;
        return n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (out_wsrc) *out_wsrc = nullptr;
        return 0;
    }
#else
    (void)src; (void)out_wsrc;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// ★ v1.8：MX(DLC) 单元显示名 —— 运行时映射字典
//
//  【为什么不能用 UNTN】
//    UNTN 不是键值表，而是 DUNT 的**伴生数组**：实测 394 条与本体
//    Data/unittype.cam 的 DUNT 394 条逐位 100% 对应（下标 i 的单元码相同）。
//    即「下标 = 单元索引」。DLC 的 mx_Unittype.cam 记录追加在其后，
//    但 UNTN 一旦改条数/顺序，本体单元就会整体错位 → 建筑错乱甚至消失。
//    （v1.7 之前按单元码排序插入 121 条 → 建筑消失，已回滚，勿重蹈。）
//
//  【DLC 单元名的真正来源】
//    mx_Unittype.cam 的 DUNT 记录内嵌 ASCII 显示名（Mausoleum / Magic Bazaar ...）。
//    所以在这里于「赋值入口」做完整字符串精确匹配替换，零数据改动。
//
//  【误伤控制】
//    只在整个字符串**完全相等**时替换；且首版仅收录建筑/巢穴/生物/英雄类专名
//    （AB*/BB*/BV*/AV*/WV*）。法术类 XR*/XL* 多为 Fire / Gate / Stones 等通用词，
//    暂不收录，待专名验证通过后再评估。
// ---------------------------------------------------------------------------
struct MxNameEntry { const char* en; const char* cn; };

static const MxNameEntry g_mxNames[] = {
    {"Siege Marker", "围攻标记"},            // ABA1
    {"Dark Palace", "黑暗宫殿"},             // ABJ4
    {"Magic Bazaar", "魔法集市"},            // ABl1
    {"Magic Bazaar Level 2", "2级魔法集市"},  // ABl2
    {"Magic Bazaar Level 3", "3级魔法集市"},  // ABl3
    {"Outpost", "前哨"},                    // ABm1
    {"Hall Of Champions", "英杰殿"},         // ABo1
    {"Mausoleum", "陵墓"},                  // ABp1
    {"Sorcerers Abode", "巫师住所"},         // ABq1
    {"Sorcerers Abode Level 2", "2级巫师住所"}, // ABq2
    {"Sorcerers Abode Level 3", "3级巫师住所"}, // ABq3
    {"Embassy", "使馆"},                    // ABr1
    {"Gnome Champion", "地精勇士"},          // AVO1
    {"Ancient Barrow", "远古墓冢"},          // BBO1
    {"Archaic Tomb", "古冢"},               // BBP1
    {"Fortress of Ixmil", "伊克斯米尔要塞"},  // BBQ1
    {"Snake Pit", "蛇窟"},                  // BBS1
    {"Spire Of Death", "死亡尖塔"},          // BBT1
    {"Ancient Graveyard", "远古墓地"},       // BBu1
    {"Broken Sewer Main", "破损下水道"},     // BBv1
    {"Ice Cave", "冰穴"},                   // BBw1
    {"Rat's Nest", "鼠巢"},                 // BBx1
    {"Goblin Watchtower", "小妖精哨塔"},     // BBy1
    {"Goblin Fortress", "小妖精堡垒"},       // BBz1
    {"The Abomination", "憎恶"},            // BVl1
    {"Ice Dragon", "冰龙"},                 // BVm1
    {"Goblin Overlord", "小妖精霸主"},       // BVo1
    {"Greater Gorgon", "高阶戈贡"},          // BVp1
    {"Ratapult", "鼠投石车"},               // BVq1
    {"Ratman Champion", "鼠精战士"},         // BVr1
    {"Ratman Shaman", "鼠精萨满"},           // BVs1
    {"Rhoden the Rat King", "鼠王罗登"},     // BVt1
    {"Shadowbeast", "暗影兽"},              // BVu1
    {"Stones", "石块"},                     // BVv1
    {"Styx", "斯蒂克斯"},                    // BVw1
    {"Yeti", "雪人"},                       // BVx1
    {"Wendigo", "温迪戈"},                   // BVy1
    {"Gate", "传送门"},                      // WVh1
    {"Earthquake", "地震术"},                // WVi1
    {"Critical Hit", "致命一击"},            // WVj1
};

// ★ v1.9：可由 MajestyI_TextFix.ini 的 [Settings] MxNameDict=0 一键关闭。
static int g_mxDictOn = 1;
static int g_uiDictOn = 1;   // ★ v1.11：EXE 内置 UI 串汉化开关（UiNameDict=0 关闭）

static void MxLoadConfig(void)
{
    char path[MAX_PATH] = { 0 };
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&MxLoadConfig, &self) && self)
    {
        GetModuleFileNameA(self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
    }
    strncat_s(path, MAX_PATH, "MajestyI_TextFix.ini", _TRUNCATE);
    g_mxDictOn = GetPrivateProfileIntA("Settings", "MxNameDict", 1, path);
    g_uiDictOn = GetPrivateProfileIntA("Settings", "UiNameDict", 1, path);  // ★ v1.11
}

// ★★★ v1.10：只替换「显示名槽」，绝不碰「内部名槽」 ★★★
//
//   DUNT 记录里 name[0]=内部资源名（脚本/美术键）、name[1]=显示名。
//   v1.8/v1.9 文本全等匹配会把两个槽都换掉；name[0] 一被改成中文，
//   建造脚本就找不到该单位 → 弹窗 "Tried to access non-existent attribute type"。
//   （实测：MxNameDict=1 时能看到「陵墓」但无法建造，正是这个原因。）
//
//   两者文本内容可能完全相同（Mausoleum/Mausoleum、Outpost/Outpost ...），
//   所以只能靠**返回地址**区分。v1.9 实测：
//     0x005AB62C —— 40 个条目全部命中且恒为 ×2 → 显示名槽 name[1]  ✔ 只在这里替换
//     0x005AB50C —— 仅在 name[0]==name[1] 的条目出现        → 内部名槽 name[0]  ✘ 跳过
//   ★ v2.0：这两个返回地址**不再硬编码** —— 由 sigscan.cpp 在运行期用 AOB
//   特征码派生（定位名槽 thunk → 在体内找「调用 Assign」的 E8 rel32 →
//   返回地址 = 调用指令 + 5）。这样 Steam / GOG 两版共用一份 asi。
//   同一版可能派生多个（存在多个同构 thunk），所以用集合。
//   ★ 失败即空集 → 一个都不替换（fail-closed）。这是刻意的：
//     v1.8 那种「解析不到就全局替换」会把内部名槽一起改掉，导致单位无法建造。
//
//   实测派生结果（两版一致，仅地址不同）：
//     显示名槽 name[1] : Steam 0x005AB62C / GOG 0x005BF92C
//     内部名槽 name[0] : Steam 0x005AB50C / GOG 0x005BF80C
#define MX_MAX_RET 8
static uintptr_t g_mxDispRet[MX_MAX_RET];  static int g_mxDispRetN = 0;  // 显示名槽 → 替换中文
static uintptr_t g_mxIntRet [MX_MAX_RET];  static int g_mxIntRetN  = 0;  // 内部名槽 → 放行英文

static bool MxRetInSet(const uintptr_t* set, int n, uintptr_t r)
{
    for (int i = 0; i < n; ++i)
        if (set[i] == r) return true;
    return false;
}

// 完整字符串精确匹配 + 调用点白名单；命中返回中文(UTF-8)，否则返回 nullptr。
static const char* MxNameLookup(const char* src, int bytes, void* ret)
{
    if (!g_mxDictOn || !src || bytes <= 0 || bytes > 63)
        return nullptr;

    uintptr_t r = (uintptr_t)ret;
    if (MxRetInSet(g_mxIntRet, g_mxIntRetN, r))
        return nullptr;                     // 内部名槽：一律放行英文
    if (!MxRetInSet(g_mxDispRet, g_mxDispRetN, r))
        return nullptr;                     // 非显示名槽（或名槽未解析）→ 不替换

    for (int i = 0; i < (int)(sizeof(g_mxNames) / sizeof(g_mxNames[0])); ++i)
    {
        const char* en = g_mxNames[i].en;
        int n = (int)strlen(en);
        if (n == bytes && memcmp(src, en, (size_t)n) == 0)
        {
            LogRaw("MXHIT  ret=0x%08X  tbl=%d  en=\"%s\" -> cn=\"%s\"",
                   (unsigned)r, i, en, g_mxNames[i].cn);
            return g_mxNames[i].cn;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ★ v1.11：EXE 内置 UI 字符串汉化（分辨率设置界面）
//
//   与 MX 单元名不同，这些串都是**完整句子**，唯一性极高，因此不做返回地址
//   白名单限制，纯完整串精确匹配即可。译文对齐《王权 2》官方简中术语。
// ---------------------------------------------------------------------------
static const MxNameEntry g_uiNames[] = {
    { "SCREEN RESOLUTION", "屏幕分辨率" },
    { "Change Mode",       "更改模式" },
    { "The DirectX 9 mode change will be applied on application restart.",
                           "DirectX 9 模式更改将在重新启动游戏后生效。" },
    { "Resolution Changed","屏幕分辨率已更改" },
    { "Keep the new resolution?", "保留新的分辨率？" },
};

// 完整串精确匹配；命中返回中文(UTF-8)，否则 nullptr。
static const char* UiNameLookup(const char* src, int bytes)
{
    if (!g_uiDictOn || !src || bytes <= 0 || bytes > 127)
        return nullptr;
    for (int i = 0; i < (int)(sizeof(g_uiNames) / sizeof(g_uiNames[0])); ++i)
    {
        const char* en = g_uiNames[i].en;
        int n = (int)strlen(en);
        if (n == bytes && memcmp(src, en, (size_t)n) == 0)
        {
            LogRaw("UIHIT  tbl=%d  en=\"%s\" -> cn=\"%s\"", i, en, g_uiNames[i].cn);
            return g_uiNames[i].cn;
        }
    }
    return nullptr;
}

// ★ v1.11 Hook #7：sub_4B4840 —— 控件文本设置
//
//   int __thiscall sub_4B4840(_DWORD** this /*ecx*/, int a2, const char* a3)
//     { return (*(**(this+1) + 100))(*(this+1), a2, 15, 0, a3); }
//   "SCREEN RESOLUTION" 直接以 const char* 传入 a3，**不经 StrObj 赋值**，
//   所以必须在这一层替换。
typedef int (__thiscall *UiSetTextFn)(void* self, int a2, const char* a3);
static UiSetTextFn g_orig_4B4840 = nullptr;
static volatile LONG g_n_uidict = 0;

static int __fastcall Hooked_4B4840(void* self, void* /*edx*/, int a2, const char* a3)
{
#if STRFIX_UI_TEXT_DICT
    if (a3)
    {
        const char* cn = UiNameLookup(a3, (int)strlen(a3));
        if (cn)
        {
            InterlockedIncrement(&g_n_uidict);
            return g_orig_4B4840(self, a2, cn);
        }
    }
#endif
    return g_orig_4B4840(self, a2, a3);
}

// ---------------------------------------------------------------------------
// ★ v1.7 Hook #6：sub_628240 —— 逐字节 strlen 版窄串赋值
//
//   __thiscall StrObj* sub_628240(StrObj* self /*ecx*/, const char* src)
//     原逻辑：v2 = 逐字节 strlen(src) → 清 wide 位 → sub_627890(v2) 窄分配
//             → memcpy(data, src, v2+1) → [+8] = v2 → return self(eax)
//     ★ 调用方依赖 eax = self（与 sub_627AD0 同理），detour 必须返回 self。
// ---------------------------------------------------------------------------
static StrObj* __fastcall Hooked_628240(StrObj* self, void* /*edx 占位*/, const char* src)
{
#if STRFIX_MX_NAME_DICT
    if (self && src)
    {
        int n = (int)strlen(src);
        const char* cn = MxNameLookup(src, n, _ReturnAddress());
        if (cn)
            InterlockedIncrement(&g_n_mxdict);
        else if ((cn = UiNameLookup(src, n)) != nullptr)
            InterlockedIncrement(&g_n_uidict);
        if (cn)
        {
            if (TryStoreAsWide(self, cn, (int)strlen(cn), "mxdict/628240"))
                return self;
        }
    }
#endif

#if STRFIX_628240_WIDE_FIX
    if (self && src)
    {
        const wchar_t* wsrc  = nullptr;
        int            wchars = DetectTruncatedUtf16Le(src, &wsrc);
        if (wchars > 0)
        {
            InterlockedIncrement(&g_n_trunc);
            if (TryStoreUtf16Wide(self, wsrc, wchars, "628240"))
                return self;               // ★ 与原函数一致：eax = self
        }
    }
#endif
    return g_orig_628240(self, src);
}

// ===========================================================================
// ★★★★★ v1.6：消费者侧自愈 —— 保证「测量必前进」不变量
// ===========================================================================
//
//  【为什么需要】
//  生产侧（sub_627A80 / sub_627AD0）只能覆盖「经过它们的赋值」。实测存在
//  第 3 条未 hook 的赋值路径（sub_627B10 已排除，其余未定位），
//  「自定义玩法手动设置」的坏对象正是这样来的：
//      StrObj:  data = D8 9E A4 8B 00     (UTF-16LE "默认")
//               [+4] = 0x00000004          (len = 4, flags = 0x00 → ANSI!)
//               [+8] = 4
//  声明 ANSI，内容却是 UTF-16LE → 窄表查不到 0xD8 的字形 →
//  sub_66CA70 溢出且不推进 → sub_66CDA0 的 while(1) 永转（无迭代上限）。
//
//  【思路】不再追生产者，改为在【测量消费者的唯一入口】做一次性形态自愈，
//  保证引擎接下来读到的 StrObj 满足「一定能前进」：
//    A. flags=wide 但扫描上限内无 0x0000 终止符  → 强制空串（宽扫描立即终止）
//    B. flags=narrow 但内容像 UTF-16LE            → 转 wide（★ 保留文本，主修复）
//    C. flags=narrow 但内容是合法 UTF-8 多字节    → 转 wide（生产侧漏网兜底）
//    D. flags=narrow、含高位字节、两种解码都不成立：
//         D1 全部字节都是 Latin-1 可打印 → byte→wchar 转 wide（保留西文）
//         D2 含 C1 控制符(0x80..0x9F)    → 强制空串（宽表同样缺字形）
//  自愈是幂等的：修好后 flags 与内容自洽，下次走快速路径 O(1) 返回。
//
//  【为什么选这两个点】
//    · 二者是「多行行高」与「文本像素宽」的唯一实现，IDA xrefs 显示它们
//      被 sub_66D0B0 / sub_66D450 / sub_66E7B0 共同调用 —— 覆盖面完整
//    · 都从 this[0] 取 StrObj*，与 sub_66CDA0/66CE40 的读取逻辑同构
//    · sub_66CDA0 是唯一【无迭代上限】的 while(1)（sub_66CE40 受 a2 约束）
//      → 本次死循环正是它
// ===========================================================================

// 自愈日志：前 STRFIX_HEAL_LOG_DETAIL 条逐条打印，之后每 1000 条一次
static inline bool HealWantsLog(LONG n)
{
    return (n <= STRFIX_HEAL_LOG_DETAIL) || ((n % 1000) == 0);
}

// 返回 true = 修改了对象（调用方无需额外处理，接着调原函数即可）
static bool HealStrObjForMeasure(StrObj* s, const char* tag, void* caller)
{
#if !STRFIX_HEAL_CONSUMER
    (void)s; (void)tag; (void)caller;
    return false;
#else
    if (!s || !s->data) return false;
    if ((uintptr_t)s->data < 0x10000u) return false;       // 静态空串缓冲/异常指针

    const unsigned char rawFlags = StrObj_GetFlags(s);
    const int           len      = StrObj_GetLen(s);

    // ===================== A. 对象声明为 wide =====================
    if (rawFlags & STROBJ_FLAG_WIDE)
    {
        if (len <= 0) return false;

        // 快速路径（O(1)）：引擎约定终止符位于 data[len]
        //   （sub_6279E0 拷贝 2*len+2 字节；本 DLL 的 TryStoreAsWide 写 dst[n]=0）
        bool fastTerm = false;
        __try
        {
            if (len <= STRFIX_HEAL_SCAN_WORDS)
            {
                const wchar_t* w = (const wchar_t*)s->data;
                if (w[len] == 0) fastTerm = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { fastTerm = false; }

        if (fastTerm) return false;

        // 慢速路径：在 [0 .. min(len, 上限)] 内找第一个 0x0000。
        //   found: -2 = 读内存异常（不确定，不动）  -1 = 扫完仍无终止符  >=0 = 找到
        //   ★ 快查与扫描分成两个 __try：快查越界（len 偏大）时仍能靠扫描
        //     在更早的位置找到终止符，避免把合法长串误判成坏对象。
        int found = -2;
        __try
        {
            const wchar_t* w = (const wchar_t*)s->data;
            int limit = (len < STRFIX_HEAL_SCAN_WORDS) ? len : STRFIX_HEAL_SCAN_WORDS;
            found = -1;
            for (int i = 0; i <= limit; ++i)
            {
                if (w[i] == 0) { found = i; break; }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { found = -2; }

        if (found != -1) return false;      // 正常 或 无法判定 → 一律不动

        // ★ 危险：wide 但扫描上限内找不到 0x0000 → 宽扫描必然死循环
        __try { ((wchar_t*)s->data)[0] = 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        StrObj_SetLen(s, 0);
        s->length2 = 0;
        InterlockedIncrement(&g_n_heal);
        InterlockedIncrement(&g_n_heal_wide);
        {
            LONG n = g_n_heal;
            if (HealWantsLog(n))
                LogRaw("HEAL[%s] A: wide 无终止符 → 强制空串  self=%p data=%p  len=%d flags=%02X caller=%p",
                       tag, s, s->data, len, rawFlags, caller);
        }
        return true;
    }

    // ===================== B/C/D. 对象声明为 narrow =====================
    if (len <= 0 || len > STRFIX_MAX_SRC_BYTES) return false;

    // 先判「有没有高位字节」。纯 ASCII 一定安全（窄表必有字形）→ 直接放行。
    const unsigned char* b = (const unsigned char*)s->data;
    bool anyHigh = false;
    __try
    {
        for (int i = 0; i < len; ++i)
            if (b[i] >= 0x80) { anyHigh = true; break; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    if (!anyHigh) return false;

    // ---- B. 内容像 UTF-16LE（★ 本次「自定义玩法手动设置」的主修复路径）----
    //     data = D8 9E A4 8B → SniffUtf16Le 返回 2（"默认"）
    {
        int wch = SniffUtf16Le((const char*)s->data, len);
        if (wch > 0)
        {
            InterlockedIncrement(&g_n_utf16);
            InterlockedIncrement(&g_n_sniff);
            if (TryStoreUtf16Wide(s, (const wchar_t*)s->data, wch, tag))
            {
                InterlockedIncrement(&g_n_heal);
                InterlockedIncrement(&g_n_heal_narrow);
                LONG n = g_n_heal;
                if (HealWantsLog(n))
                    LogRaw("HEAL[%s] B: narrow→wide(UTF16LE)  self=%p  原 len=%d → n=%d  新 data=%p caller=%p",
                           tag, s, len, wch, s->data, caller);
                return true;
            }
        }
    }

    // ---- C. 内容是合法 UTF-8 多字节（生产侧漏网时的兜底）----
    {
        int badAt = -1;
        if (IsValidUtf8WithMultibyte((const char*)s->data, len, &badAt))
        {
            InterlockedIncrement(&g_n_utf8);
            if (TryStoreAsWide(s, (const char*)s->data, len, tag))
            {
                InterlockedIncrement(&g_n_heal);
                InterlockedIncrement(&g_n_heal_narrow);
                LONG n = g_n_heal;
                if (HealWantsLog(n))
                    LogRaw("HEAL[%s] C: narrow→wide(UTF8)    self=%p  原 len=%d  新 data=%p caller=%p",
                           tag, s, len, s->data, caller);
                return true;
            }
        }
    }

    // ---- D1. 全部字节都是 Latin-1 可打印 → 1:1 映射转 wide（保留西文）----
#if STRFIX_HEAL_LATIN1
    if (len <= 1024)
    {
        bool latinOk = true;
        __try
        {
            for (int i = 0; i < len; ++i)
            {
                unsigned char c = b[i];
                if (c >= 0x80 && c < 0xA0) { latinOk = false; break; }   // C1 控制符 → 宽表无字形
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { latinOk = false; }

        if (latinOk)
        {
            wchar_t tmp[1024];
            for (int i = 0; i < len; ++i) tmp[i] = (wchar_t)b[i];
            if (TryStoreUtf16Wide(s, tmp, len, tag))
            {
                InterlockedIncrement(&g_n_heal);
                InterlockedIncrement(&g_n_heal_narrow);
                LONG n = g_n_heal;
                if (HealWantsLog(n))
                    LogRaw("HEAL[%s] D1: narrow→wide(Latin1)  self=%p  n=%d  新 data=%p caller=%p",
                           tag, s, len, s->data, caller);
                return true;
            }
        }
    }
#endif

    // ---- D2. 兜底：含 C1 控制符等「宽表必然缺字形」的字节 → 强制空串 ----
#if STRFIX_HEAL_EMPTY_JUNK
    {
        char dump[256];
        DumpHex((const char*)s->data, len, 32, dump, sizeof(dump));

        __try { ((unsigned char*)s->data)[0] = 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        StrObj_SetLen(s, 0);
        s->length2 = 0;

        InterlockedIncrement(&g_n_heal);
        InterlockedIncrement(&g_n_heal_junk);
        LONG n = g_n_heal;
        if (HealWantsLog(n))
            LogRaw("HEAL[%s] D2: narrow 无法解码 → 强制空串  self=%p data=%p len=%d hex[%s] caller=%p",
                   tag, s, s->data, len, dump, caller);
        return true;
    }
#else
    return false;
#endif
#endif // STRFIX_HEAL_CONSUMER
}

// ---------------------------------------------------------------------------
// ★ Hook #4（v1.6）：sub_66CDA0 —— 多行文本的行数/行高计算  ★ 唯一无迭代上限的循环
//
//   int __thiscall sub_66CDA0(int** this, int a2)   ; ret 4
//     this[0]      = StrObj*  主字符串
//     this[1]/[2]  = 内联短串及其 wide 标志
//     this[10]     = 有效性标志（为 0 直接返回 0）
//   循环：读字符 → 为 0 则返回 → sub_66CA70 测宽 → v5 = sub_66C7D0(位置)
//         若 0xD8 这类窄字节查不到字形，位置不前进 → while(1) 永转（本次现场）
// ---------------------------------------------------------------------------
static int __fastcall Hooked_66CDA0(int** self, void* /*edx 占位*/, int a2)
{
    if (self && g_exe_base)
    {
        StrObj* s = (StrObj*)self[0];
        if (s)
        {
            LONG m = InterlockedIncrement(&g_n_measure);
            HealStrObjForMeasure(s, "66CDA0", *(void**)_AddressOfReturnAddress());
            if ((m % 100000) == 0)
                LogRaw("STAT 测量=%ld 自愈=%ld(wide无终止符=%ld 已转wide=%ld 强制空串=%ld)"
                       " 生产侧hitU=%ld sniff=%ld hit8=%ld 清零=%ld ascii=%ld 截断修复=%ld",
                       m, g_n_heal, g_n_heal_wide, g_n_heal_narrow, g_n_heal_junk,
                       g_n_utf16, g_n_sniff, g_n_utf8, g_n_zeroed, g_n_ascii, g_n_trunc);
        }
    }
    return g_orig_66CDA0(self, a2);
}

// ---------------------------------------------------------------------------
// ★ Hook #5（v1.6）：sub_66CE40 —— 文本像素宽度计算
//
//   int __thiscall sub_66CE40(int** this, int a2, int a3)   ; ret 8
//   与 sub_66CDA0 同构，只是循环受 a2（计数）约束，因此本身不会死循环；
//   但仍会因字形缺失而「算不出正确宽度」，一并自愈。
// ---------------------------------------------------------------------------
static int __fastcall Hooked_66CE40(int** self, void* /*edx 占位*/, int a2, int a3)
{
    if (self && g_exe_base)
    {
        StrObj* s = (StrObj*)self[0];
        if (s)
            HealStrObjForMeasure(s, "66CE40", *(void**)_AddressOfReturnAddress());
    }
    return g_orig_66CE40(self, a2, a3);
}

// ---------------------------------------------------------------------------
// ★ Hook #1：sub_627A80 —— StrObj 的 strlen 版 ANSI 赋值（配置串/标签名等）
//
//   __thiscall StrObj* sub_627A80(StrObj* self /*ecx*/, const char* src)
//   原逻辑：[+4]=0 → strlen(src) → sub_627890(len) 分配 → memcpy → [+8]=len
//   ★ 返回 self（eax）。调用方依赖这个返回值！hook 必须忠实透传。
// ---------------------------------------------------------------------------
static StrObj* __fastcall Hooked_627A80(StrObj* self, void* /*edx 占位*/, const char* src)
{
    InterlockedIncrement(&g_n_total);

    if (!self || !src)
        return g_orig_627A80(self, src);

    int srcBytes = (int)strlen(src);

    // ---- ★★★ v1.8：MX(DLC) 单元显示名运行时映射 ----
    //   DLC 单元名只存在于 mx_Unittype.cam 的 DUNT 记录内嵌 ASCII 名里；
    //   而 UNTN 是 DUNT 的伴生数组（下标即单元索引，394 条与本体 DUNT 逐位对应），
    //   无法安全插入新条目（改条数/顺序会让本体单元整体错位 → 建筑消失）。
    //   故改为在赋值点做「完整字符串精确匹配」替换，完全不碰任何 CAM 数据。
#if STRFIX_MX_NAME_DICT
    {
        const char* cn = MxNameLookup(src, srcBytes, _ReturnAddress());
        if (cn)
            InterlockedIncrement(&g_n_mxdict);
        else if ((cn = UiNameLookup(src, srcBytes)) != nullptr)
            InterlockedIncrement(&g_n_uidict);
        if (cn)
        {
            if (TryStoreAsWide(self, cn, (int)strlen(cn), "mxdict/627A80"))
                return self;
        }
    }
#endif

    // ---- ★★★ v1.4：CAM 的 UTF-16LE 路径 ----
    // 特征：BOM(FF FE) 紧邻 src 之前（或 src 自身即 BOM）。
    // 这种源不是合法 UTF-8，之前会被原样转发 → 生成 ANSI 容器装 UTF-16 字节
    // → 测宽溢出 → 死循环。
    {
        int bomSkip = DetectUtf16LeSource(src, srcBytes);
        if (bomSkip >= 0)
        {
            int payload = srcBytes - bomSkip;
            if (payload >= 2 && (payload % 2) == 0)
            {
                const wchar_t* wsrc = (const wchar_t*)(src + bomSkip);
                int maxw = payload / 2, wchars = 0;
                while (wchars < maxw && wsrc[wchars] != 0) ++wchars;
                if (wchars > 0)
                {
                    InterlockedIncrement(&g_n_utf16);
                    if (TryStoreUtf16Wide(self, wsrc, wchars, "627A80"))
                        return self;
                }
            }
        }
    }

    int badAt = -1;
    bool needConvert = IsValidUtf8WithMultibyte(src, srcBytes, &badAt);

    if (!needConvert)
    {
        // ---- ★★★ v1.5：非法 UTF-8 + 「内容像 UTF-16LE」→ 直接按宽串存储 ----
        //   CAM 路径的源串不带 BOM，BOM 判定漏网；这里靠内容形态兜住。
        if (badAt >= 0)
        {
            int wch = SniffUtf16Le(src, srcBytes);
            if (wch > 0)
            {
                InterlockedIncrement(&g_n_utf16);
                InterlockedIncrement(&g_n_sniff);
                if (TryStoreUtf16Wide(self, (const wchar_t*)src, wch, "627A80/sniff"))
                    return self;
            }
        }

        LONG n = InterlockedIncrement(&g_n_ascii);
#if LOG_LEVEL >= 3
        {
            char dump[512];
            DumpBytes(src, srcBytes, LOG_DUMP_MAX_BYTES, dump, sizeof(dump));
            LogRaw("A80 pass self=%p len=%d flags=%02X src=\"%s\"%s",
                   self, srcBytes, StrObj_GetFlags(self), dump,
                   (badAt >= 0 ? "  [非法UTF8，原样转发]" : ""));
        }
#elif LOG_COUNT_MISS
        if (n % 1000 == 0)
            LogRaw("A80 pass 累计 %ld 次（最近一条 len=%d）", n, srcBytes);
#endif
        (void)n;
        return g_orig_627A80(self, src);
    }

    InterlockedIncrement(&g_n_utf8);
    if (TryStoreAsWide(self, src, srcBytes, "627A80"))
        return self;               // ★ 与原函数一致：返回 self
    return g_orig_627A80(self, src);   // 失败回退
}

// ---------------------------------------------------------------------------
// ★ Hook #2：sub_627AD0 —— StrObj 的「字节区间」赋值（★ 中文文本主路径）
//
//   __thiscall StrObj* sub_627AD0(StrObj* self /*ecx*/, const char* src, int count)
//   原逻辑：[+4]=0 → sub_627890(count) 分配 → memcpy(data, src, count) → [+8]=count
//   实测：XML 文本节点 <Text>激活模组</Text> 的内容正是从这里进入 StrObj 的。
//   ★ 返回 self（eax）。调用方（如 0x68D4CC 会把返回值当 src 传给 CopyFrom）
//     强依赖这个返回值；若不返回 self 会导致野指针 AV 崩溃（已实测踩坑）。
// ---------------------------------------------------------------------------
static StrObj* __fastcall Hooked_627AD0(StrObj* self, void* /*edx 占位*/, const char* src, int count)
{
    InterlockedIncrement(&g_n_total);

#if STRFIX_MX_NAME_DICT
    if (self && src && count > 0)
    {
        // 用 count 而不是 strlen：本入口按字节区间赋值，源未必以 0 结尾
        const char* cn = MxNameLookup(src, count, _ReturnAddress());
        if (cn)
            InterlockedIncrement(&g_n_mxdict);
        else if ((cn = UiNameLookup(src, count)) != nullptr)
            InterlockedIncrement(&g_n_uidict);
        if (cn)
        {
            if (TryStoreAsWide(self, cn, (int)strlen(cn), "mxdict/627AD0"))
                return self;
        }
    }
#endif

    if (!self || !src || count <= 0 || count > STRFIX_MAX_SRC_BYTES)
        return g_orig_627AD0(self, src, count);

    // ---- ★★★ v1.4：CAM 的 UTF-16LE 路径（★ 本次死循环的实际来源）----
    //   CAM 字符串记录 = [key][FF FE][UTF-16LE 正文][00 00]，
    //   而加载器把「正文起点」(BOM+2) 当 const char* 交给本函数，
    //   且 count = 字节数（= 2×字符数）→ 得到 ANSI 容器装 UTF-16 字节：
    //        data = D8 9E A4 8B 00 (UTF-16LE "默认")  flags=0x00(ANSI)  len=4
    //   测宽按单字节读 0xD8/0x9E… 宽度表查不到 → 溢出 → 位置不前进 → 死循环。
    {
        int bomSkip = DetectUtf16LeSource(src, count);
        if (bomSkip >= 0)
        {
            int payload = count - bomSkip;
            if (payload >= 2 && (payload % 2) == 0)
            {
                const wchar_t* wsrc = (const wchar_t*)(src + bomSkip);
                int maxw = payload / 2, wchars = 0;
                while (wchars < maxw && wsrc[wchars] != 0) ++wchars;
                if (wchars > 0)
                {
                    InterlockedIncrement(&g_n_utf16);
                    if (TryStoreUtf16Wide(self, wsrc, wchars, "627AD0"))
                        return self;
                }
            }
        }
    }

    int badAt = -1;
    bool needConvert = IsValidUtf8WithMultibyte(src, count, &badAt);

    if (!needConvert)
    {
        // ---- ★★★ v1.5：非法 UTF-8 + 「内容像 UTF-16LE」→ 直接按宽串存储 ----
        //   本次「自定义玩法手动设置」死循环的实际入口：
        //     src = D8 9E A4 8B (UTF-16LE "默认", 无 BOM)  count = 4
        //   IsValidUtf8WithMultibyte 在 0xA4 处判定非法 → badAt=2；
        //   嗅探认为「按 UTF-16LE 解读是 0x9ED8("默") 0x8BA4("认")」→ 按宽串存。
        if (badAt >= 0)
        {
            int wch = SniffUtf16Le(src, count);
            if (wch > 0)
            {
                InterlockedIncrement(&g_n_utf16);
                InterlockedIncrement(&g_n_sniff);
                if (TryStoreUtf16Wide(self, (const wchar_t*)src, wch, "627AD0/sniff"))
                    return self;
            }
        }

        LONG n = InterlockedIncrement(&g_n_ascii);
#if LOG_LEVEL >= 3
        {
            char dump[512];
            DumpBytes(src, count, LOG_DUMP_MAX_BYTES, dump, sizeof(dump));
            LogRaw("AD0 pass self=%p cnt=%d src=\"%s\"%s",
                   self, count, dump,
                   (badAt >= 0 ? "  [非法UTF8，原样转发]" : ""));
        }
#elif LOG_COUNT_MISS
        if (n % 2000 == 0)
            LogRaw("AD0 pass 累计 %ld 次（最近一条 cnt=%d）", n, count);
#endif
        (void)n;
        return g_orig_627AD0(self, src, count);
    }

    InterlockedIncrement(&g_n_utf8);
    if (TryStoreAsWide(self, src, count, "627AD0"))
        return self;               // ★ 与原函数一致：返回 self
    return g_orig_627AD0(self, src, count);   // 失败回退
}

// ---------------------------------------------------------------------------
// ★★★★★ Hook #3（v1.2 根治点）：sub_627890 —— StrObj 缓冲分配器
//
//   void __thiscall sub_627890(StrObj* self /*ecx*/, int count)
//
//   这是全引擎【唯一】的 StrObj 缓冲分配入口（IDA 确认 17 个调用者，
//   全部位于 StrObj 实现簇 0x6279xx–0x62Bxxx）。
//
//   【死循环真凶】
//   原分配器：wide → malloc(2n+4)，data=malloc+2，写 0xFEFF BOM；
//             narrow → malloc(n+1)，data=malloc；
//             ★ 只写 [+4] 低 24 位（保留 flags 高字节）
//             ★ 【不清零新缓冲】
//   于是「只分配、不填充内容」的路径（预留/复用）会留下：
//             flags = isWide(1) + len = n + data = 满 0xBAADF00D 的未初始化堆
//   而 0xBAADF00D / 0xFEEEFEEE 这些填充值【每个字节都非零】，宽扫描永远
//   读不到 0x0000 终止符 → sub_66C7D0 不前进 → 死循环。
//
//   【根治手段】分配后立即把整块缓冲清零。
//     · 后续写内容的正常路径 → 行为完全不变（内容照写）
//     · 「只分配不填充」的路径 → 变成空串（首字符 '\0'）→ 扫描立即终止
//     · 不清 BOM（BOM 在 data-2，不在清零范围）→ wide 语义保持
//     · 连 flags 与内容不匹配的情形也一并兜住（残留区是 0 → 必有终止符）
//
//   ★★★ v1.3 修正：必须原样回传原函数在 eax 里的返回值！
//     原函数签名是 `int __thiscall(void** this, int a2)`，结尾是
//         `result = (a2 ^ *(this+1)) & 0xFFFFFF;  return result;`
//     即 **eax = 新长度（24bit）**。v1.2 误把 detour 写成 void，之后
//     memset / InterlockedIncrement / LogRaw 都会覆盖 eax，调用方若消费
//     该返回值就会拿到垃圾。
// ---------------------------------------------------------------------------
static int __fastcall Hooked_627890(StrObj* self, void* /*edx 占位*/, int count)
{
    StrAllocFn orig = g_orig_627890;

    // ---- 记录「进入时是否是 wide」：决定清零的字节宽度 ----
    int wideIn = 0;
    unsigned char rawFlags = 0;
    if (self)
    {
        rawFlags = StrObj_GetFlags(self);
        wideIn   = (rawFlags & STROBJ_FLAG_WIDE) ? 1 : 0;
    }

    // ---- 调用原分配器（★ 保存返回值，最后原样回传） ----
    int rv = orig(self, count);

    if (!self || !self->data)
        return rv;

#if STRFIX_ZERO_ON_ALLOC
    // ---- ★ 清零整块缓冲 ----
    // wide  : malloc(2n+4)，data=malloc+2 → 可用 data..data+2n+1 = (n+1) 个 wchar
    // narrow: malloc(n+1)，data=malloc   → 可用 data..data+n   = (n+1) 个字节
    // 两个尺寸都【刚好装满】，不会越界（narrow 的终止符就是 data[n]，
    // 见 sub_627AD0: `mov byte ptr [edi+edx], 0`）。
    // count==0 时原函数用静态空串缓冲（unk_7BA43A/unk_7C8858），绝不能 memset；
    // malloc 失败时原函数会写 data = NULL+2 = 2，也必须排除。
    if (count > 0 && count <= STRFIX_ZERO_MAX_CHARS &&
        (uintptr_t)self->data > 0x10000u)
    {
        size_t bytes = wideIn ? (size_t)(count + 1) * 2 : (size_t)(count + 1);
        memset(self->data, 0, bytes);
        InterlockedIncrement(&g_n_zeroed);
    }
#endif

#if STRFIX_SANITIZE_FLAGS
    // ---- 清洗 flags 里的已知堆填充垃圾 ----
    // 必须在原函数之后做：原函数要用 isWide 决定分配分支（已用 wideIn 记录），
    // 这里再把 0xBA/0xCD/… 之类的垃圾高字节抹掉，避免下游误判 wide。
    if (rawFlags != 0)
    {
        unsigned char clean = SanitizeFlags(rawFlags);
        if (clean != rawFlags)
        {
            StrObj_SetFlags(self, clean);
#if LOG_LEVEL >= 3
            LogRaw("ALLOC flags 清洗 %p: %02X -> %02X (count=%d)", self, rawFlags, clean, count);
#endif
        }
    }
#endif

    return rv;      // ★ 与原函数一致：eax = 新长度
}


// ---------------------------------------------------------------------------
// 观测用：sub_627B10（区间子串赋值）—— 不改行为，只记录
// ---------------------------------------------------------------------------
#if HOOK_WIDE_ASSIGN_OBSERVE
static void __fastcall Hooked_627B10(StrObj* self, void* /*edx*/,
                                     const char* src, int a, int b, int c, int d)
{
    if (self && src)
        LogRaw("B10 observe self=%p flags=%02X len=%d args=(%d,%d,%d,%d)",
               self, StrObj_GetFlags(self), StrObj_GetLen(self), a, b, c, d);
    g_orig_627B10(self, src, a, b, c, d);
}
#endif

// ---------------------------------------------------------------------------
// ★ v2.0：AOB 特征码定位模块（Steam / GOG 双发行版自适应）
//   以 #include 方式接入，不单独编译（无需改 vcxproj）。
//   它负责把下面所有 g_addr_* 填好，并派生 MX 名槽返回地址集合。
// ---------------------------------------------------------------------------
#include "sigscan.cpp"

// 定位失败（g_addr_* == 0）时跳过该点，绝不阻断其它 hook。
#define TRY_HOOK(NAME, ADDR, DETOUR, ORIG)                                        \
    do {                                                                          \
        if (!(ADDR)) {                                                            \
            LogRaw("--  跳过 %s：特征码未定位", NAME);                            \
        } else if (MH_CreateHook((LPVOID)(ADDR), (LPVOID)&(DETOUR),               \
                                 (LPVOID*)&(ORIG)) != MH_OK) {                    \
            LogRaw("!!  MH_CreateHook(%s @ %p) 失败", NAME, (void*)(ADDR));       \
        } else {                                                                  \
            LogRaw("OK  MH_CreateHook(%s @ %p) -> trampoline = %p",               \
                   NAME, (void*)(ADDR), (void*)(ORIG));                           \
        }                                                                         \
    } while (0)

// ---------------------------------------------------------------------------
// 安装 Hook
//
//   ★ v2.0：所有目标地址改由 sigscan.cpp 的 AOB 特征码在运行期定位，
//           不再有任何「基址 + 固定 RVA」的运算。
//   任一目标定位失败（命中 0 处 → 找不到；≥2 处 → 有歧义）→ 对应 g_addr_* = 0
//   → 跳过该 hook，其余 hook 照常启用。游戏以原版行为继续跑，
//   **绝不**把 hook 装到错误地址上（这是安全失败的底线）。
// ---------------------------------------------------------------------------
static bool InstallHooks(void)
{
    const bool allLocated = SigLocateAll();
    LogRaw("--  特征码定位结果：%d/%d%s", g_sig_ok, g_sig_total,
           allLocated ? "  （全部命中）" : "  !! 存在未命中项，对应 hook 将被跳过");

    if (MH_Initialize() != MH_OK)
    {
        LogRaw("!! MH_Initialize 失败");
        return false;
    }

    // --- 逐个安装 ---
    //  ★ v2.0：定位失败的点自动跳过（日志写明），其余照常启用；
    //     不再像旧版那样「一个失败就 return false 导致全盘不装」。
    //     顺序按重要性排：分配器清零 / 测量自愈 是物理杜绝死循环的两道保险。
    TRY_HOOK("sub_627890 Alloc★根治    ", g_addr_627890, Hooked_627890, g_orig_627890);
    TRY_HOOK("sub_66CDA0 测量·行数/行高", g_addr_66CDA0, Hooked_66CDA0, g_orig_66CDA0);
    TRY_HOOK("sub_66CE40 测量·像素宽  ", g_addr_66CE40, Hooked_66CE40, g_orig_66CE40);
    TRY_HOOK("sub_627AD0 AssignRange  ", g_addr_627AD0, Hooked_627AD0, g_orig_627AD0);
    TRY_HOOK("sub_627A80 Assign(ANSI) ", g_addr_627A80, Hooked_627A80, g_orig_627A80);
    TRY_HOOK("sub_628240 截断修复 v1.7", g_addr_628240, Hooked_628240, g_orig_628240);
    TRY_HOOK("sub_4B4840 EXE UI 串    ", g_addr_4B4840, Hooked_4B4840, g_orig_4B4840);
#if HOOK_WIDE_ASSIGN_OBSERVE
    TRY_HOOK("sub_627B10 (仅观测)    ", g_addr_627B10, Hooked_627B10, g_orig_627B10);
#endif

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        LogRaw("!! MH_EnableHook 失败");
        return false;
    }

    LogRaw("OK  所有可用的 hook 已启用（特征码定位 %d/%d）", g_sig_ok, g_sig_total);
    return true;
}

// ---------------------------------------------------------------------------
// DLL 入口
// ---------------------------------------------------------------------------
static DWORD WINAPI InitThread(LPVOID)
{
    InitializeCriticalSection(&g_log_cs);
    LogOpen();
    MxLoadConfig();

    LogRaw("================================================================");
    LogRaw(" MajestyI_StrFix  v2.0  (AOB 特征码定位 · Steam / GOG 双发行版自适应)");
    LogRaw(" v2.0: ★ 全部硬编码 VA 改为运行期 AOB 特征码扫描 ——");
    LogRaw("       旧实现 (基址 + 固定RVA) 在 GOG 版上每个 hook 都会落错地址；");
    LogRaw("       现由 8 条通配签名在两版 .text 全段唯一命中自动定位，");
    LogRaw("       命中 0 处/多处 → 跳过该 hook（安全失败，绝不误 hook）。");
    LogRaw("       验证脚本: scripts/aob_verify.py（不依赖 IDA，直接扫 exe 字节）");
    LogRaw(" 目标: 修复 XML/MQXML/CAM 中文因被当 ANSI 处理而导致的测宽死循环");
    LogRaw(" 落点: sub_627A80(strlen赋值) + sub_627AD0(★字节区间赋值,中文主路径)");
    LogRaw("       + sub_627890(★★★ 分配器清零, 消灭未初始化缓冲)");
    LogRaw("       + sub_66CDA0 / sub_66CE40 (★★★★★ 消费者侧自愈, v1.6)");
    LogRaw("       + sub_628240 (★★★★★ 被 strlen 截断的 UTF-16LE 修复, v1.7)");
    LogRaw(" v1.4: CAM UTF-16LE 识别 —— 记录形态 [key][FF FE][U16正文][0000]");
    LogRaw(" v1.5: 内容嗅探兜底 —— 无 BOM 时靠形态判定");
    LogRaw(" v1.6: ★ 生产侧追不到第3条赋值路径 → 改为在【测量必经入口】自愈:");
    LogRaw("       A) wide 无终止符 → 强制空串    B) narrow 内容像 UTF-16LE → 转 wide");
    LogRaw("       C) narrow 内容像 UTF-8 → 转 wide   D) Latin-1 → wide / 垃圾 → 空串");
    LogRaw(" v1.7: ★ 独一无二→*r 根因 = sub_628240 逐字节 strlen 截断 UTF-16LE:");
    LogRaw("       一=U+4E00 低字节 0x00 → strlen 停在第2字节 → 只留 '独' → \"*r\"");
    LogRaw(" v1.10: ★ MX 名槽返回地址同样改为特征码派生（thunk → 体内 call Assign → ret）");
    LogRaw(" 日志级别 LOG_LEVEL=%d  清零=%d 清洗flags=%d 识别UTF16=%d 嗅探=%d 自愈=%d",
           LOG_LEVEL, STRFIX_ZERO_ON_ALLOC, STRFIX_SANITIZE_FLAGS,
           STRFIX_DETECT_UTF16, STRFIX_SNIFF_UTF16, STRFIX_HEAL_CONSUMER);
    LogRaw(" 自愈选项: Latin1=%d 强空串兜底=%d 扫描上限=%d words  截断修复=%d",
           STRFIX_HEAL_LATIN1, STRFIX_HEAL_EMPTY_JUNK, STRFIX_HEAL_SCAN_WORDS,
           STRFIX_628240_WIDE_FIX);
    LogRaw(" v1.9: MX 译名映射=%d (ini [Settings] MxNameDict=0 可关)  命中会打印 ret=返回地址",
           g_mxDictOn);
    LogRaw("================================================================");

    // 等 EXE 主模块完全就绪（ASI loader 注入时 EXE 已映射，这里主要是保险）
    HMODULE exe = nullptr;
    for (int i = 0; i < 200; ++i)
    {
        exe = GetModuleHandleA(nullptr);
        if (exe) break;
        Sleep(10);
    }

    if (exe)
    {
        if (InstallHooks())
        {
            InterlockedExchange(&g_ready, 1);
            if (g_sig_ok == g_sig_total)
                LogRaw("钩子就绪（%d/%d 全部定位），开始放行游戏加载流程。", g_sig_ok, g_sig_total);
            else
                LogRaw("钩子部分就绪（%d/%d）：未定位的修复本次不生效，游戏仍可正常游玩。"
                       "请把上方 !! 行反馈给开发者。", g_sig_ok, g_sig_total);
        }
        else
        {
            LogRaw("!! 钩子安装失败（MH_Initialize/EnableHook 层面），本次运行未受修复保护。");
        }
    }
    else
    {
        LogRaw("!! 拿不到 EXE 模块句柄，放弃挂钩。");
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 关键：DllMain 里不能做加载/分配，另起线程装 hook
        {
            HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
            if (h) CloseHandle(h);
        }
        break;

    case DLL_PROCESS_DETACH:
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (InterlockedCompareExchange(&g_ready, 0, 1) == 1)
            LogRaw("---- DLL 卸载 ----");
        LogClose();
        DeleteCriticalSection(&g_log_cs);
        break;
    }
    return TRUE;
}
