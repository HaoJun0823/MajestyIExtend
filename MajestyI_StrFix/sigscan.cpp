// ===========================================================================
// sigscan.cpp —— 运行期 AOB（特征码）定位：Steam / GOG 双发行版自适应
//
//   ★ 本文件由 dllmain.cpp 以 #include 方式接入（不单独编译，无需改 vcxproj）。
//
// ---------------------------------------------------------------------------
// 背景
// ---------------------------------------------------------------------------
//  Majesty HD 有两个发行版 exe，同源 + 同编译器（MSVCR90/MSVCP90）两次构建：
//      MajestyHD.exe      (Steam)  2017-02-05  SizeOfImage 0x40E000
//      MajestyHD_GOG.exe  (GOG)    2018-10-08  SizeOfImage 0x42D000
//  两版**放在同一目录、共用同一份 Data/**，且都静态导入 WINMM.dll ——
//  同一个 Ultimate ASI Loader 会向两版都注入同一个 scripts\*.asi，
//  因此「一份 .asi 必须在运行期自适应」。
//
//  代码整体位移，但位移量分成 5 簇，绝无「统一偏移」可用：
//      +0x000EF0  UI 区        (sub_4B4840)
//      +0x0118F0  StrObj 区    (sub_627xxx 簇)
//      +0x011950  文本度量区   (sub_66Cxxx 簇)
//      +0x014300  DUNT 名槽区  (sub_5ABxxx 簇)
//      +0x014650  文本消费     (sub_5D7720)
//  旧实现写的是 base + (固定VA - 0x400000)，在 GOG 版上**每一个 hook 都会落到
//  错误地址**。本文件替换掉全部硬编码地址计算。
//
// ---------------------------------------------------------------------------
// 方案：AOB 特征码扫描（用户选型）
// ---------------------------------------------------------------------------
//  签名取自函数序言/函数体，把「随版本漂移」的字节写成通配 ??：
//      E8 ?? ?? ?? ??      相对调用 rel32（被调函数地址变了）
//      68 ?? ?? ?? ??      push 绝对地址（SEH handler / 字符串常量）
//      A1 ?? ?? ?? ??      mov eax, ds:[绝对地址]（栈 Cookie）
//      FF 15 ?? ?? ?? ??   call ds:[IAT]（两版 IAT 位置不同）
//  通配掉这些位置后，签名在两版之间稳定（实测）。
//
//  自带自校验（这是本方案相对「发行版查表」的核心优势）：
//      命中 0 处 → 找不到；命中 ≥2 处 → 有歧义 —— 两者都判**失败**，
//      失败则**不安装该 hook**，游戏退化为原版行为继续跑。
//      宁可少一个修复，也绝不 hook 到错误地址（此前 code cave 方案在
//      0x5D7B1E 上崩溃，就是这个性质的事故）。
//
// ---------------------------------------------------------------------------
// 独立验证（不依赖 IDA）
// ---------------------------------------------------------------------------
//  scripts/aob_verify.py 直接读两个 exe 的字节做交叉验证，结论：
//    · 8 条代码签名在两版 .text 全段**唯一命中**，且精确落在期望地址；
//    · DUNT 名槽 thunk 区域「通配 rel32 后」两版**逐字节一致**。
//
// ---------------------------------------------------------------------------
// DUNT 名槽：结构性派生，不硬编码返回地址
// ---------------------------------------------------------------------------
//  名槽赋值走两个同构 thunk（成员 [+0Ch] = 内部名槽 / [+10h] = 显示名槽）。
//  thunk 的短签名每版命中 2 处（同构 thunk 有多个），无法直接判定。
//  做法：
//    ① 用短签名找出全部候选；
//    ② 在候选函数体内寻找「E8 rel32 且目标 == 已定位的 Assign 函数」的那条调用；
//    ③ 返回地址 = 该调用指令地址 + 5。
//  这是**结构性验证**：靠函数间的调用关系（确定性），不靠字节偏移的运气。
//  实测每版 2 个候选里只有 1 个调用了 Assign，另一条（改写别的成员、走别的
//  赋值函数）被这条判据自动排除。
// ===========================================================================

// ---------------------------------------------------------------------------
// 期望地址表（★ 仅用于日志比对，让「本次到底跑的是哪一版」一目了然 ——
//              绝不参与 hook 地址计算）
// ---------------------------------------------------------------------------
#define IMG_EXPECT_BASE   0x00400000u

// ---------------------------------------------------------------------------
// 特征码表
// ---------------------------------------------------------------------------
#define SIG_627A80 \
    "53 8B 5C 24 08 56 57 8B F9 C7 47 04 00 00 00 00 33 F6 80 3B 00 8B C3 74 0E"
#define SIG_627AD0 \
    "56 57 8B 7C 24 10 8B F1 57 C7 46 04 00 00 00 00 E8 ?? ?? ?? ?? 8B 06 8B 4C 24 0C 57 50 51 E8"
#define SIG_627890 \
    "56 8B F1 F6 46 07 01 57 8B 7C 24 0C 74 4B 85 FF 74 2E 8D 44 3F 04 50 FF 15"
#define SIG_628240 \
    "53 8B 5C 24 08 56 33 F6 57 8B F9 85 DB 74 0C 80 3B 00 74 07 46 80 3C 1E 00 75 F9 8B 4F 04"
#define SIG_4B4840 \
    "8B 54 24 08 8B 49 04 8B 01 8B 40 64 52 8B 54 24 08 6A 00 6A 0F 52 FF D0 C2 08 00"
#define SIG_66CDA0 \
    "83 EC 08 55 56 57 8B 7C 24 18 33 ED 8B F1 83 FF FF 75 06 8B 7E 14 2B 7E 0C 39 2E 75 05"
#define SIG_66CE40 \
    "51 55 8B 6C 24 10 56 8B F1 83 FD FF 75 06 8B 6E 14 2B 6E 0C 83 3E 00 75 0A 83 7E 04 00"
#define SIG_627B10 \
    "53 55 8B 6C 24 14 56 33 C0 8B F1 57 89 46 04 83 FD 01 74 51"

// DUNT 名槽 thunk 短签名（E8 通配；+0Ch = 内部名槽 → 跳过，+10h = 显示名槽 → 替换）
#define SIG_THUNK_INT \
    "8B F9 8B 77 0C 85 F6 74 10 8B CE E8 ?? ?? ?? ?? 56 E8 ?? ?? ?? ?? 83 C4 04 8B 74 24 1C C7 47 0C 00 00 00 00"
#define SIG_THUNK_DISP \
    "8B F9 8B 77 10 85 F6 74 10 8B CE E8 ?? ?? ?? ?? 56 E8 ?? ?? ?? ?? 83 C4 04 8B 74 24 1C C7 47 10 00 00 00 00"

// ---------------------------------------------------------------------------
// 特征码引擎
// ---------------------------------------------------------------------------
#define SIG_MAXLEN      128
#define SIG_MX_PROBE   0x120u   // 在 thunk 候选体内探测「调用 Assign」的字节窗口

struct SigPat
{
    unsigned char b[SIG_MAXLEN];   // 期望字节
    unsigned char w[SIG_MAXLEN];   // 1 = 通配
    int           len;
};

struct SigImage
{
    const unsigned char* text;      // .text 段在内存中的起点
    size_t               textSize;  // 可扫描字节数
    uintptr_t            textVa;    // .text 段起始 VA
    uintptr_t            base;      // 模块基址（用于把 VA 折算回 RVA 做版本比对）
};

static int SigHexNib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "53 8B ?? 24" → bytes/wildcard 数组
static bool SigParse(const char* s, SigPat* p)
{
    p->len = 0;
    while (s && *s)
    {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
        if (!*s) break;
        if (p->len >= SIG_MAXLEN) return false;

        if (*s == '?')
        {
            p->b[p->len] = 0;
            p->w[p->len] = 1;
            ++p->len;
            ++s;
            if (*s == '?') ++s;
            continue;
        }
        int hi = SigHexNib(s[0]);
        int lo = SigHexNib(s[1]);
        if (hi < 0 || lo < 0) return false;

        p->b[p->len] = (unsigned char)((hi << 4) | lo);
        p->w[p->len] = 0;
        ++p->len;
        s += 2;
    }
    return p->len > 0;
}

// 解析主模块 PE → 取 .text 段（映射映像内的字节 + VA）。失败返回 false。
//
//   ★ 关键：这里读的是**已映射到内存的映像**，所以「相对基址的偏移 == RVA」，
//     .text 的起点是 base + VirtualAddress —— 不是 base + PointerToRawData！
//     （后者是文件里的原始偏移，映射后已不再适用。踩过这个坑。）
static bool SigInitImage(HMODULE hMod, SigImage* img)
{
    if (!hMod) return false;

    const unsigned char* b = (const unsigned char*)hMod;
    if (b[0] != 'M' || b[1] != 'Z') return false;

    unsigned int e = *(const unsigned int*)(b + 0x3C);
    if (e < 0x40 || e > 0x1000) return false;
    if (b[e] != 'P' || b[e + 1] != 'E' || b[e + 2] != 0 || b[e + 3] != 0) return false;

    const unsigned char* coff = b + e + 4;
    unsigned short nsec  = *(const unsigned short*)(coff + 2);
    unsigned short optsz = *(const unsigned short*)(coff + 16);
    const unsigned char* opt = coff + 20;
    const unsigned char* sec = opt + optsz;

    unsigned int sizeOfImage = *(const unsigned int*)(opt + 56);
    if (nsec == 0 || nsec > 96 || sizeOfImage == 0) return false;

    for (unsigned short i = 0; i < nsec; ++i)
    {
        const unsigned char* s = sec + 40 * (size_t)i;
        if (!(s[0] == '.' && s[1] == 't' && s[2] == 'e' && s[3] == 'x' && s[4] == 't'))
            continue;

        unsigned int vsz = *(const unsigned int*)(s + 8);
        unsigned int va  = *(const unsigned int*)(s + 12);
        unsigned int rsz = *(const unsigned int*)(s + 16);

        unsigned int n = (vsz > rsz) ? vsz : rsz;   // 映射后两者对应的页都已提交
        if (va >= sizeOfImage) return false;
        if (n > sizeOfImage - va) n = sizeOfImage - va;   // 绝不越出映像范围
        if (n == 0) return false;

        img->text     = b + va;
        img->textSize = n;
        img->textVa   = (uintptr_t)b + va;
        img->base     = (uintptr_t)b;
        return true;
    }
    return false;
}

// 从 text 相对偏移 fromOff 起找下一处命中；返回相对偏移，无则 -1
static long SigFindNext(const SigImage* img, const SigPat* p, size_t fromOff)
{
    if (!img->text || p->len <= 0) return -1;

    const size_t n = img->textSize;
    if (n < (size_t)p->len) return -1;

    int anchor = 0;                                  // 选第一个非通配字节做预筛
    while (anchor < p->len && p->w[anchor]) ++anchor;
    if (anchor >= p->len) return -1;

    const size_t last = n - (size_t)p->len;
    size_t i = fromOff;

    while (i <= last)
    {
        const unsigned char* q = (const unsigned char*)memchr(
            img->text + i + (size_t)anchor, p->b[anchor], last - i + 1);
        if (!q) return -1;

        size_t s = (size_t)(q - img->text) - (size_t)anchor;

        bool ok = true;
        for (int k = 0; k < p->len; ++k)
        {
            if (!p->w[k] && img->text[s + (size_t)k] != p->b[k]) { ok = false; break; }
        }
        if (ok) return (long)s;

        i = s + 1;
    }
    return -1;
}

// 唯一命中 → 返回 VA；命中 0 处或多处 → 返回 0 并写日志（安全失败）
static uintptr_t SigScanUnique(const SigImage* img, const char* pat,
                               const char* name, uintptr_t steamVa, uintptr_t gogVa)
{
    SigPat p;
    if (!SigParse(pat, &p))
    {
        LogRaw("!!  %s 特征码解析失败（内部错误）", name);
        return 0;
    }

    long o1 = SigFindNext(img, &p, 0);
    if (o1 < 0)
    {
        LogRaw("!!  %s 特征码未命中（0 处）→ 跳过该 hook", name);
        return 0;
    }

    long o2 = SigFindNext(img, &p, (size_t)o1 + 1);
    if (o2 >= 0)
    {
        LogRaw("!!  %s 特征码命中多处（%08X / %08X …）→ 有歧义，跳过该 hook",
               name, (unsigned)(img->textVa + (size_t)o1),
               (unsigned)(img->textVa + (size_t)o2));
        return 0;
    }

    uintptr_t va = img->textVa + (size_t)o1;
    unsigned  rva = (unsigned)(va - img->base);

    const char* ver = "未知构建 ⚠";
    if (rva == (unsigned)(steamVa - IMG_EXPECT_BASE))      ver = "Steam ✔";
    else if (rva == (unsigned)(gogVa - IMG_EXPECT_BASE))   ver = "GOG ✔";

    LogRaw("OK  %s = %08X  (RVA %06X)  %s", name, (unsigned)va, rva, ver);
    return va;
}

// 在 [fromVa, fromVa+limit) 内查找「E8 rel32 且目标 == want」的调用，
// 返回该 call 指令所在 VA；找不到返回 0。
// 判据强度：需 rel32 精确算出目标地址（1/2^32 误配率），足够安全。
static uintptr_t SigFindCallTo(const SigImage* img, uintptr_t fromVa, size_t limit, uintptr_t want)
{
    if (fromVa < img->textVa) return 0;

    size_t off = (size_t)(fromVa - img->textVa);
    if (off >= img->textSize || img->textSize < 5) return 0;

    size_t end = off + limit;
    const size_t last = img->textSize - 5;
    if (end > last) end = last;

    for (size_t i = off; i <= end; ++i)
    {
        const unsigned char* q = img->text + i;
        if (q[0] != 0xE8) continue;

        unsigned int rel = (unsigned int)q[1]
                         | ((unsigned int)q[2] << 8)
                         | ((unsigned int)q[3] << 16)
                         | ((unsigned int)q[4] << 24);

        uintptr_t tgt = (uintptr_t)(img->textVa + i + 5) + (intptr_t)(int)rel;
        if (tgt == want) return img->textVa + i;
    }
    return 0;
}

// 求候选 thunk 的函数结尾（首个 C2 04 00 = retn 4），返回 text 相对偏移。
//
//   ★ 必须把「找调用」限制在本函数内：thunk 之间只隔 0x90 字节且高度同构，
//     若用固定窗口探测，会跨进下一个 thunk 的函数体，把「别人的」返回地址
//     也算成自己的（实测踩过：内部名槽集合里混进了显示名槽的返回地址，
//     后果是显示名永远不替换 —— 因为内部名槽集合先命中就放行了）。
static size_t SigFuncEndOff(const SigImage* img, size_t fromOff)
{
    const size_t n = img->textSize;
    for (size_t i = fromOff; i + 2 < n; ++i)
    {
        if (img->text[i] == 0xC2 && img->text[i + 1] == 0x04 && img->text[i + 2] == 0x00)
            return i + 3;
    }
    return n;
}

// ---------------------------------------------------------------------------
// DUNT 名槽：解析出「赋值调用点的返回地址」集合（可多个）
//   内部名槽 (+0Ch) → g_mxIntRet[] （放行英文）
//   显示名槽 (+10h) → g_mxDispRet[]（替换中文）
// ---------------------------------------------------------------------------
static void SigResolveMxOne(const SigImage* img, const char* pat, const char* tag,
                            uintptr_t assignVa, uintptr_t* set, int* n)
{
    SigPat p;
    if (!SigParse(pat, &p))
    {
        LogRaw("!!  MX %s 特征码解析失败", tag);
        return;
    }

    size_t from = 0;
    int cand = 0, used = 0;

    for (;;)
    {
        long o = SigFindNext(img, &p, from);
        if (o < 0) break;
        from = (size_t)o + 1;
        ++cand;

        uintptr_t hit = img->textVa + (size_t)o;

        // ★ 只在本 thunk 函数体内找「调用 Assign」的那条 rel32
        size_t span = SigFuncEndOff(img, (size_t)o) - (size_t)o;
        if (span > SIG_MX_PROBE) span = SIG_MX_PROBE;

        uintptr_t call = SigFindCallTo(img, hit, span, assignVa);

        if (!call)
        {
            LogRaw("    MX %s 候选#%d @%08X 体内(长度%u)未调用 Assign → 非名槽 thunk，跳过",
                   tag, cand, (unsigned)hit, (unsigned)span);
            continue;
        }

        uintptr_t ret = call + 5;
        if (!MxRetInSet(set, *n, ret) && *n < MX_MAX_RET)
        {
            set[(*n)++] = ret;
            ++used;
            LogRaw("    MX %s 候选#%d @%08X  call %08X → ret=%08X  ✔",
                   tag, cand, (unsigned)hit, (unsigned)call, (unsigned)ret);
        }
    }

    if (used == 0)
        LogRaw("!!  MX %s 未解析出返回地址（候选 %d 个）→ 该槽不做替换（安全放行英文）",
               tag, cand);
    else
        LogRaw("OK  MX %s 解析出 %d 个返回地址", tag, used);
}

static void SigResolveMxRets(const SigImage* img)
{
    g_mxIntRetN  = 0;
    g_mxDispRetN = 0;

    if (!g_mxDictOn)
    {
        LogRaw("--  MX 译名映射已由 ini 关闭，跳过名槽解析");
        return;
    }
    if (!g_addr_627A80)
    {
        LogRaw("!!  MX 名槽：Assign(sub_627A80) 未定位 → 无法派生名槽返回地址，"
               "MX(DLC) 译名本次不生效（安全降级）");
        return;
    }

    LogRaw("--  MX 名槽派生（锚点 = Assign @ %08X）", (unsigned)g_addr_627A80);
    SigResolveMxOne(img, SIG_THUNK_INT,  "内部名槽(+0Ch)", g_addr_627A80, g_mxIntRet,  &g_mxIntRetN);
    SigResolveMxOne(img, SIG_THUNK_DISP, "显示名槽(+10h)", g_addr_627A80, g_mxDispRet, &g_mxDispRetN);
}

// ---------------------------------------------------------------------------
// 主入口：定位全部 hook 目标（写入 dllmain.cpp 的 g_addr_* 全局）
//   任一目标 ≥2 处命中 / 0 处命中 → 该 g_addr_* 保持 0，对应 hook 安装时跳过。
// ---------------------------------------------------------------------------
static int g_sig_ok = 0;
static int g_sig_total = 0;

// moduleBase = 主模块映像基址。拆出这一层是为了让扫描逻辑可以被
// 独立测试程序直接喂「手工映射的映像」调用（见 .temp/sigtest/）。
static bool SigLocateAllAt(void* moduleBase)
{
    g_exe_base = (HMODULE)moduleBase;
    if (!moduleBase)
    {
        LogRaw("!!  GetModuleHandle(NULL) 失败");
        return false;
    }

    SigImage img;
    if (!SigInitImage((HMODULE)moduleBase, &img))
    {
        LogRaw("!!  解析主模块 .text 段失败，无法做特征码定位");
        return false;
    }

    LogRaw("--  模块基址 = %p   .text = %08X..%08X (%u KB)",
           moduleBase, (unsigned)img.textVa,
           (unsigned)(img.textVa + img.textSize), (unsigned)(img.textSize / 1024));

    struct SigTarget
    {
        const char* name;
        uintptr_t   steamVa;
        uintptr_t   gogVa;
        const char* pat;
        uintptr_t*  out;
    };

    const SigTarget T[] = {
        { "sub_627A80 Assign(ANSI) ", 0x627A80u, 0x63C370u, SIG_627A80, &g_addr_627A80 },
        { "sub_627AD0 AssignRange  ", 0x627AD0u, 0x63C3C0u, SIG_627AD0, &g_addr_627AD0 },
        { "sub_627890 Alloc ★根治  ", 0x627890u, 0x63C180u, SIG_627890, &g_addr_627890 },
        { "sub_628240 NarrowAssign ", 0x628240u, 0x63CB30u, SIG_628240, &g_addr_628240 },
        { "sub_66CDA0 MeasureLines ", 0x66CDA0u, 0x6816F0u, SIG_66CDA0, &g_addr_66CDA0 },
        { "sub_66CE40 MeasureWidth ", 0x66CE40u, 0x681790u, SIG_66CE40, &g_addr_66CE40 },
        { "sub_4B4840 UiSetText    ", 0x4B4840u, 0x4B5730u, SIG_4B4840, &g_addr_4B4840 },
        { "sub_627B10 AssignSub    ", 0x627B10u, 0x63C400u, SIG_627B10, &g_addr_627B10 },
    };

    g_sig_ok = 0;
    g_sig_total = (int)(sizeof(T) / sizeof(T[0]));

    for (int i = 0; i < g_sig_total; ++i)
    {
        uintptr_t a = SigScanUnique(&img, T[i].pat, T[i].name, T[i].steamVa, T[i].gogVa);
        *T[i].out = a;                 // ★ 失败时显式为 0 → 后续跳过该 hook
        if (a) ++g_sig_ok;
    }

    SigResolveMxRets(&img);

    return g_sig_ok == g_sig_total;
}

// 生产入口：以主模块（游戏 EXE）为扫描对象
static bool SigLocateAll(void)
{
    return SigLocateAllAt((void*)GetModuleHandleA(nullptr));
}
