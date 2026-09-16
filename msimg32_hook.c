#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>

#pragma comment(linker, "/NODEFAULTLIB")
#pragma comment(linker, "/ENTRY:DllMain")
#pragma comment(linker, "/export:AlphaBlend=msimg32_orig.AlphaBlend")
#pragma comment(linker, "/export:GradientFill=msimg32_orig.GradientFill")
#pragma comment(linker, "/export:TransparentBlt=msimg32_orig.TransparentBlt")
#pragma comment(linker, "/export:vSetDdrawflag=msimg32_orig.vSetDdrawflag")
#pragma comment(linker, "/export:DllInitialize=msimg32_orig.DllInitialize")
#pragma function(memcpy, memset, memcmp)

#define HM_UNINSTALLTOOL 0xE6C3FF0Du

typedef struct {
    unsigned rva;
    unsigned char old[8];
    unsigned char new_[8];
    unsigned n;
} UT_PATCH;

static const UT_PATCH kPatches[] = {
    {0x9DB0u,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x00, 0x00},
     {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0x00, 0x00},
     6u},
    {0x9F74u,
     {0xE8, 0x4F, 0x49, 0x2D, 0x00, 0x8B, 0xE8, 0x83},
     {0xB8, 0x03, 0x00, 0x00, 0x00, 0x8B, 0xE8, 0x83},
     5u},
    /* license object: mov al,[rcx+0x16c]; ret -> mov al,1; ret */
    {0x34720u,
     {0x8A, 0x81, 0x6C, 0x01, 0x00, 0x00, 0xC3, 0x00},
     {0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x00},
     7u},
    /* license type: mov eax,[rcx+0x188]; ret -> mov eax,1; ret */
    {0x346DCu,
     {0x8B, 0x81, 0x88, 0x01, 0x00, 0x00, 0xC3, 0x00},
     {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x00},
     7u},
    /* serial-verified getter: cmp helper result,3 -> al=1 */
    {0x34934u,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB},
     {0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90},
     8u},
};

static HMODULE g_orig;
static volatile LONG g_done;
static volatile LONG g_iat_done;
static void *g_settext;
static void *g_sendmsg;
static void *g_cwex;

typedef BOOL(WINAPI *FnSetWindowTextW)(HWND, LPCWSTR);
typedef LRESULT(WINAPI *FnSendMessageW)(HWND, UINT, WPARAM, LPARAM);
typedef HWND(WINAPI *FnCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
    int, int, HWND, HMENU, HINSTANCE, LPVOID);

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q)
            return (int)*p - (int)*q;
        p++;
        q++;
    }
    return 0;
}

static size_t wlen(const wchar_t *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static const wchar_t *wfind(const wchar_t *h, const wchar_t *n)
{
    size_t i, j;
    if (!h || !n || !n[0])
        return 0;
    for (i = 0; h[i]; ++i) {
        for (j = 0; n[j] && h[i + j] == n[j]; ++j)
            ;
        if (!n[j])
            return h + i;
    }
    return 0;
}

static int weq(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const wchar_t *rewrite_text(const wchar_t *src, wchar_t *tmp, size_t tmpcch)
{
    const wchar_t *hit;
    size_t n, keep;

    if (!src)
        return src;
    if (weq(src, L"UNREGISTERED VERSION"))
        return L"Registered";
    if (wfind(src, L"Trial Expired") || wfind(src, L"Trial Reminder"))
        return L"";
    hit = wfind(src, L" [UNREGISTERED VERSION]");
    if (!hit)
        hit = wfind(src, L"[UNREGISTERED VERSION]");
    if (!hit)
        return src;
    n = (size_t)(hit - src);
    if (n + 1u >= tmpcch)
        return src;
    keep = n;
    memcpy(tmp, src, keep * sizeof(wchar_t));
    tmp[keep] = 0;
    return tmp;
}

static unsigned hmix(unsigned h, unsigned char b)
{
    h ^= (unsigned)b;
    h = ((h << 7) | (h >> 25)) + 0x6D2B79F5u;
    h ^= h >> 11;
    return h;
}

static unsigned h_wfnv(const wchar_t *s, unsigned nbytes)
{
    unsigned h = 0xA5A5C3E1u;
    unsigned n, i;
    if (!s || nbytes < 2)
        return 0;
    n = nbytes / 2u;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)(s[i] & 0xFF);
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c + 32);
        h = hmix(h, c);
    }
    return h;
}

typedef struct _USTR {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} USTR;

typedef struct _LDR {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    USTR FullDllName;
    USTR BaseDllName;
} LDR;

typedef struct _PEB_LDR {
    ULONG Length;
    UCHAR Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
} PEB_LDR;

typedef struct _PEB {
    UCHAR Reserved1[2];
    UCHAR BeingDebugged;
    UCHAR Reserved2[1];
    PVOID Reserved3[2];
    PEB_LDR *Ldr;
} PEB;

static void *peb_ptr(void)
{
    return (void *)__readgsqword(0x60);
}

static unsigned host_exe_hash(void)
{
    PEB *peb = (PEB *)peb_ptr();
    LIST_ENTRY *head, *cur;
    LDR *e;
    if (!peb || !peb->Ldr)
        return 0;
    head = &peb->Ldr->InLoadOrderModuleList;
    cur = head->Flink;
    if (!cur || cur == head)
        return 0;
    e = (LDR *)cur;
    if (!e->BaseDllName.Buffer || e->BaseDllName.Length < 4)
        return 0;
    return h_wfnv(e->BaseDllName.Buffer, e->BaseDllName.Length);
}

static BYTE *host_base(void)
{
    PEB *peb = (PEB *)peb_ptr();
    if (peb && peb->Ldr) {
        LIST_ENTRY *head = &peb->Ldr->InLoadOrderModuleList;
        LIST_ENTRY *cur = head->Flink;
        if (cur && cur != head) {
            LDR *e = (LDR *)cur;
            if (e->DllBase)
                return (BYTE *)e->DllBase;
        }
    }
    return 0;
}

static int patch_bytes(BYTE *dst, const unsigned char *old, const unsigned char *new_, unsigned n)
{
    DWORD old_prot, tmp;
    if (!dst || !old || !new_ || !n)
        return 0;
    if (memcmp(dst, old, n) != 0)
        return 0;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old_prot))
        return 0;
    memcpy(dst, new_, n);
    VirtualProtect(dst, n, old_prot, &tmp);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}

static BOOL WINAPI settext_hook(HWND wnd, LPCWSTR text)
{
    wchar_t buf[512];
    FnSetWindowTextW real = (FnSetWindowTextW)g_settext;
    if (!real)
        return FALSE;
    return real(wnd, rewrite_text(text, buf, 512));
}

static LRESULT WINAPI sendmsg_hook(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    wchar_t buf[512];
    FnSendMessageW real = (FnSendMessageW)g_sendmsg;
    if (!real)
        return 0;
    if (msg == WM_SETTEXT && lp)
        lp = (LPARAM)rewrite_text((const wchar_t *)lp, buf, 512);
    return real(wnd, msg, wp, lp);
}

static HWND WINAPI cwex_hook(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int cx, int cy, HWND par, HMENU menu, HINSTANCE inst, LPVOID param)
{
    FnCreateWindowExW real = (FnCreateWindowExW)g_cwex;
    HWND h;
    if (!real)
        return 0;
    if (title && (wfind(title, L"Trial Expired") || wfind(title, L"Trial Reminder")))
        style &= ~WS_VISIBLE;
    h = real(ex, cls, title, style, x, y, cx, cy, par, menu, inst, param);
    if (h && title && (wfind(title, L"Trial Expired") || wfind(title, L"Trial Reminder"))) {
        typedef BOOL(WINAPI *FnShowWindow)(HWND, int);
        HMODULE u = GetModuleHandleW(L"user32.dll");
        FnShowWindow sw = u ? (FnShowWindow)GetProcAddress(u, "ShowWindow") : 0;
        if (sw)
            sw(h, SW_HIDE);
    }
    return h;
}

static int dll_is(const char *dll, const char *want)
{
    unsigned i;
    if (!dll || !want)
        return 0;
    for (i = 0; want[i]; ++i) {
        unsigned char a = (unsigned char)dll[i];
        unsigned char b = (unsigned char)want[i];
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + 32);
        if (a != b)
            return 0;
    }
    return dll[i] == 0;
}

static int name_is(const char *fn, const char *want)
{
    unsigned i;
    if (!fn || !want)
        return 0;
    for (i = 0; want[i]; ++i) {
        if (fn[i] != want[i])
            return 0;
    }
    return fn[i] == 0;
}

static int iat_hook(BYTE *base, const char *dll_want, const char *fn_want, void *hook, void **orig)
{
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!base || !dll_want || !fn_want || !hook || !orig)
        return 0;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress)
        return 0;
    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
    for (; imp->Name; ++imp) {
        IMAGE_THUNK_DATA *thunk, *org;
        if (!dll_is((const char *)(base + imp->Name), dll_want))
            continue;
        org = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; org->u1.AddressOfData; ++org, ++thunk) {
            IMAGE_IMPORT_BY_NAME *bn;
            DWORD old, tmp;
            void **slot;
            if (IMAGE_SNAP_BY_ORDINAL64(org->u1.Ordinal))
                continue;
            bn = (IMAGE_IMPORT_BY_NAME *)(base + org->u1.AddressOfData);
            if (!name_is((const char *)bn->Name, fn_want))
                continue;
            slot = (void **)&thunk->u1.Function;
            if (!*orig)
                *orig = *slot;
            if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
                return 0;
            *slot = hook;
            VirtualProtect(slot, sizeof(void *), old, &tmp);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
            return 1;
        }
    }
    return 0;
}

static void apply_unlock(void)
{
    BYTE *base;
    unsigned i;

    if (InterlockedCompareExchange(&g_done, 1, 0) != 0)
        return;
    if (host_exe_hash() != HM_UNINSTALLTOOL)
        return;
    base = host_base();
    if (!base)
        return;
    for (i = 0; i < (unsigned)(sizeof(kPatches) / sizeof(kPatches[0])); ++i) {
        const UT_PATCH *p = &kPatches[i];
        (void)patch_bytes(base + p->rva, p->old, p->new_, p->n);
    }
    if (InterlockedCompareExchange(&g_iat_done, 1, 0) == 0) {
        (void)iat_hook(base, "user32.dll", "SetWindowTextW", (void *)settext_hook, &g_settext);
        (void)iat_hook(base, "user32.dll", "SendMessageW", (void *)sendmsg_hook, &g_sendmsg);
        (void)iat_hook(base, "user32.dll", "CreateWindowExW", (void *)cwex_hook, &g_cwex);
    }
}

static DWORD WINAPI apply_later(LPVOID p)
{
    unsigned n = 0;
    (void)p;
    for (;;) {
        apply_unlock();
        if (g_done && ++n >= 6u)
            break;
        Sleep(40u);
    }
    return 0;
}

static wchar_t *find_last_slash(wchar_t *s)
{
    wchar_t *last = NULL;
    while (s && *s) {
        if (*s == L'\\')
            last = s;
        s++;
    }
    return last;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    wchar_t path[MAX_PATH];
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if (GetModuleFileNameW(h, path, MAX_PATH)) {
            lstrcpyW(find_last_slash(path) + 1, L"msimg32_orig.dll");
            g_orig = LoadLibraryW(path);
        }
        apply_unlock();
        if (!g_done)
            QueueUserWorkItem(apply_later, 0, WT_EXECUTELONGFUNCTION);
    } else if (reason == DLL_PROCESS_DETACH && g_orig) {
        FreeLibrary(g_orig);
        g_orig = NULL;
    }
    return TRUE;
}
