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
//      sub_627A80 —— StrObj 的 ANSI 赋值函数，1316 个调用者
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
//      [+0] void*  data          [+4] int length (ANSI=字节数 / wide=字符数)
//      [+7] byte   flags (bit0 = isWide)      [+8] int length2
// ===========================================================================

#include "pch.h"
#include "config.h"
#include "strfix.h"
#include "MinHook.h"

// ---------------------------------------------------------------------------
// 原函数指针
// ---------------------------------------------------------------------------
typedef void (__thiscall *AssignAnsiFn)(StrObj* self, const char* src);
typedef void (__thiscall *StrAllocFn)  (StrObj* self, int count);

#if HOOK_WIDE_ASSIGN_OBSERVE
typedef void (__thiscall *AssignWideFn)(StrObj* self, const wchar_t* src);
static AssignWideFn g_orig_627AD0 = nullptr;
#endif

static AssignAnsiFn g_orig_627A80 = nullptr;

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_log_cs;      // 游戏可能多线程加载资源，日志要串行
static volatile LONG g_ready = 0;

// 统计
static volatile LONG g_n_total   = 0;  // 调用总数
static volatile LONG g_n_ascii   = 0;  // 纯 ASCII 转发
static volatile LONG g_n_utf8    = 0;  // 命中多字节并转换
static volatile LONG g_n_failed  = 0;  // 转换失败（回退原函数）

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

static void LogOpen(void)
{
    if (g_log) return;

    // 日志放在 ASI 所在目录（游戏根目录）下
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

    fopen_s(&g_log, path, "w");
    if (g_log)
    {
        // 行缓冲，崩溃时也能保留最后几行
        setvbuf(g_log, nullptr, _IOLBF, 0);
    }
}

static void LogClose(void)
{
    if (g_log)
    {
        fflush(g_log);
        fclose(g_log);
        g_log = nullptr;
    }
}

static void LogRaw(const char* fmt, ...)
{
    if (!g_log) return;

    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);

    EnterCriticalSection(&g_log_cs);
    fprintf(g_log, "[%02d:%02d:%02d.%03d][T%05lu] %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            (unsigned long)GetCurrentThreadId(), body);
    fflush(g_log);
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
// ★ Hook 主体：sub_627A80 —— StrObj 的 ANSI 赋值
//
//   __thiscall void sub_627A80(StrObj* self /*ecx*/, const char* src)
//   原逻辑：[+4]=0 → strlen(src) → sub_627890(len) 分配 → memcpy → [+8]=len
// ---------------------------------------------------------------------------
static void __fastcall Hooked_627A80(StrObj* self, void* /*edx 占位*/, const char* src)
{
    InterlockedIncrement(&g_n_total);

    // 防御：空指针 / 未初始化
    if (!self || !src)
    {
        g_orig_627A80(self, src);
        return;
    }

    // 取源串字节数（引擎自己也是裸 strlen，这里保持一致）
    int srcBytes = (int)strlen(src);

    int badAt = -1;
    bool needConvert = IsValidUtf8WithMultibyte(src, srcBytes, &badAt);

    if (!needConvert)
    {
        LONG n = InterlockedIncrement(&g_n_ascii);

#if LOG_LEVEL >= 3
        {
            char dump[512];
            DumpBytes(src, srcBytes, LOG_DUMP_MAX_BYTES, dump, sizeof(dump));
            LogRaw("ASCII  pass self=%p len=%d flags=%02X src=\"%s\"%s",
                   self, srcBytes, self->flags, dump,
                   (badAt >= 0 ? "  [非法UTF8，按原样转发]" : ""));
            (void)n;
        }
#elif LOG_COUNT_MISS
        // 高频路径：只在到达 1000 的倍数时打一行，证明 hook 活着
        if (n % 1000 == 0)
            LogRaw("ASCII  pass 累计 %ld 次（最近一条 len=%d）", n, srcBytes);
#else
        (void)n;
#endif
        g_orig_627A80(self, src);
        return;
    }

    // ---- 命中：含多字节 UTF-8 ----------------------------------------------
    InterlockedIncrement(&g_n_utf8);

    unsigned char oldFlags = self->flags;
    int  oldLen = self->length;

    // 转成 UTF-16
    int wchars = MultiByteToWideChar(CP_UTF8, 0, src, srcBytes, nullptr, 0);
    if (wchars <= 0)
    {
        // 转换失败 → 回退原函数，绝不改变原有行为
        InterlockedIncrement(&g_n_failed);
        LogRaw("!! UTF8->WIDE 失败 (MultiByteToWideChar 计数报错 %lu) self=%p len=%d flags=%02X → 回退原函数",
               GetLastError(), self, srcBytes, oldFlags);
        g_orig_627A80(self, src);
        return;
    }

    // ★ 关键顺序：先置 isWide，再调用引擎分配器，让分配器走 wide 语义
    self->flags = (unsigned char)(oldFlags | STROBJ_FLAG_WIDE);

    StrAllocFn engineAlloc = (StrAllocFn)g_addr_627890;
    engineAlloc(self, wchars);           // 内部 malloc(2*n+4)，写 0xFEFF BOM，回填 [+0]/[+4]

    if (!self->data)
    {
        // 分配失败 → 还原现场回退
        InterlockedIncrement(&g_n_failed);
        self->flags = oldFlags;
        self->length = oldLen;
        LogRaw("!! 引擎分配器返回空 data (wchars=%d) self=%p → 回退原函数", wchars, self);
        g_orig_627A80(self, src);
        return;
    }

    // 写数据：目标 = data + 2（跳过 0xFEFF BOM）
    wchar_t* dst = (wchar_t*)((char*)self->data + 2);
    MultiByteToWideChar(CP_UTF8, 0, src, srcBytes, dst, wchars);

    // 长度语义对齐引擎原生：wide 串 [+4]=[+8]=字符数
    self->length  = wchars;
    self->length2 = wchars;

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
        LogRaw("HIT  self=%p  ansi{len=%d flags=%02X} -> wide{n=%d}  data=%p  src=\"%s\"  w=[ %s]",
               self, srcBytes, oldFlags, wchars, self->data, srcDump, wDump);
    }
#endif
}

// ---------------------------------------------------------------------------
// 观测用：sub_627AD0（宽串赋值）—— 不改行为，只记录
// ---------------------------------------------------------------------------
#if HOOK_WIDE_ASSIGN_OBSERVE
static void __fastcall Hooked_627AD0(StrObj* self, void* /*edx*/, const wchar_t* src)
{
    if (self && src)
    {
        LogRaw("WIDE assign self=%p flags=%02X len=%d", self, self->flags, self->length);
    }
    g_orig_627AD0(self, src);
}
#endif

// ---------------------------------------------------------------------------
// 安装 Hook
// ---------------------------------------------------------------------------
static bool InstallHooks(void)
{
    g_exe_base = GetModuleHandleA(nullptr);
    if (!g_exe_base)
    {
        LogRaw("!! GetModuleHandle(NULL) 失败");
        return false;
    }

    uintptr_t base = (uintptr_t)g_exe_base;
    uintptr_t baseExpected = 0x00400000u;

    // 目标地址 = 模块基址 + (固定VA - 期望ImageBase)，兼容万一开了 ASLR
    g_addr_627A80 = base + (ADDR_STR_ASSIGN_ANSI - baseExpected);
    g_addr_627890 = base + (ADDR_STR_ALLOC       - baseExpected);
    g_addr_627AD0 = base + (ADDR_STR_ASSIGN_WIDE - baseExpected);

    LogRaw("EXE 基址 = %p (期望 %p)%s", (void*)base, (void*)baseExpected,
           (base == baseExpected ? "" : "  !! 基址与期望不一致，已按 RVA 重算"));

    // 打印目标处前 8 字节，便于人工核对是不是我们要的函数
    {
        const unsigned char* p = (const unsigned char*)g_addr_627A80;
        LogRaw("sub_627A80 @ %p  首字节: %02X %02X %02X %02X %02X %02X %02X %02X",
               (void*)g_addr_627A80, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        LogRaw("sub_627A80 期望首字节: 53 8B 5C 24 08 8B F9 C7   (push ebx / mov ebx,[esp+8] / mov edi,ecx / mov [edi+4],0)");
    }

    if (MH_Initialize() != MH_OK)
    {
        LogRaw("!! MH_Initialize 失败");
        return false;
    }

    // --- hook sub_627A80 ---
    if (MH_CreateHook((LPVOID)g_addr_627A80, (LPVOID)&Hooked_627A80,
                      (LPVOID*)&g_orig_627A80) != MH_OK)
    {
        LogRaw("!! MH_CreateHook(sub_627A80 @ %p) 失败", (void*)g_addr_627A80);
        return false;
    }
    LogRaw("OK  MH_CreateHook(sub_627A80) -> 原函数 trampoline = %p", (void*)g_orig_627A80);

#if HOOK_WIDE_ASSIGN_OBSERVE
    if (MH_CreateHook((LPVOID)g_addr_627AD0, (LPVOID)&Hooked_627AD0,
                      (LPVOID*)&g_orig_627AD0) == MH_OK)
        LogRaw("OK  MH_CreateHook(sub_627AD0, 仅观测)");
#endif

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        LogRaw("!! MH_EnableHook 失败");
        return false;
    }

    LogRaw("OK  所有 hook 已启用");
    return true;
}

// ---------------------------------------------------------------------------
// DLL 入口
// ---------------------------------------------------------------------------
static DWORD WINAPI InitThread(LPVOID)
{
    InitializeCriticalSection(&g_log_cs);
    LogOpen();

    LogRaw("================================================================");
    LogRaw(" MajestyI_StrFix  v1.0   (StrObj 收敛点 hook 方案)");
    LogRaw(" 目标: 修复 XML/MQXML/CAM 中文因被当 ANSI 处理而导致的测宽死循环");
    LogRaw(" 落点: sub_627A80 (StrObj ANSI 赋值, 1316 callers)");
    LogRaw(" 日志级别 LOG_LEVEL=%d", LOG_LEVEL);
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
            LogRaw("钩子就绪，开始放行游戏加载流程。");
        }
        else
        {
            LogRaw("!! 钩子安装失败，本次游戏运行未受修复保护。");
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
