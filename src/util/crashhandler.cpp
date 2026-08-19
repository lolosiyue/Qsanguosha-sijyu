#include "crashhandler.h"

#if defined(QSG_CRASH_HANDLER) && defined(Q_OS_WIN)

// QSG_BUILD_ID —— 由 build.ps1 每次构建前按当前 git HEAD 刷新写入 build_id.h。
// 走头文件而非编译宏:头文件内容变才能让 make 重编本文件,exe 内嵌的 id
// 因此始终与符号包文件名一致。详见 build.ps1 的 Write-BuildIdHeader。
// 不经 build.ps1 直接 qmake 时该文件可能缺失,__has_include 守卫 + 回退 unknown。
#if defined(__has_include)
#  if __has_include("build_id.h")
#    include "build_id.h"
#  endif
#endif
#ifndef QSG_BUILD_ID
#  define QSG_BUILD_ID "unknown"
#endif

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <exception>
#include <new>

// Lua C API:崩溃时只读地走出 Lua 调用栈(.lua 文件名 + 行号)。
// QSanguosha 始终带 Lua 构建(QSanguosha.pro 里 CONFIG+=lua 默认开),
// src/lua 在 INCLUDEPATH 上,故可直接 include。
#include "lua.hpp"

#ifndef RRF_RT_REG_SZ
#define RRF_RT_REG_SZ 0x00000002
#endif

namespace {

// 防重入:崩溃处理过程中若再次崩溃,直接放弃,不递归。
volatile LONG g_handling = 0;

// 进程是否已进入正常关闭流程(qApp->exec() 已返回)。
// 退出时 Lua 关闭状态机会跑 __gc 终结器,终结器经 SWIG 回调 C++ 析构,
// 期间抛出的未捕获异常会走到 terminate/SEH —— 这是退出清理阶段的崩溃,
// 玩家已主动退出、无任何损失,不是"玩到一半闪退",故不弹崩溃报告。
volatile LONG g_shuttingDown = 0;

// 启动时填充,崩溃时直接写出。8KB 足够。
char g_envInfo[8192] = {0};

// 主线程 ID 与启动时刻 —— 崩溃时用来判断崩在哪个线程、进程已运行多久。
DWORD g_mainThreadId = 0;
ULONGLONG g_startTick = 0;

// GetTickCount64 在部分 SDK 头里要 _WIN32_WINNT>=0x0600 才声明,运行时取地址绕开。
ULONGLONG tickCount64()
{
    typedef ULONGLONG (WINAPI *Fn)(void);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                      "GetTickCount64");
    return fn ? fn() : GetTickCount();
}

// 当前对局录像文件绝对路径;由 setLiveRecordPath 维护,空串表示不在对局中。
wchar_t g_liveRecord[MAX_PATH] = {0};

// 游戏版本号;由 setVersion 维护。install() 时 Engine 未就绪,故先给占位值,
// 万一在 Engine 构造完成前就崩溃,文件名/摘要里的版本段不至于为空。
char g_version[64] = "unknown";

// 崩溃那一刻的主窗口状态与游戏阶段;由 setWindowState / setGamePhase 维护。
// UI 线程写、崩溃线程读,无锁:这是尽力而为的诊断信息,撕裂读取至多得到
// 一行乱码,不影响 minidump。
char g_windowState[256] = {0};
int  g_gamePhase = 0; // 见 CrashHandler::GamePhase

// Qt 侧暂存的游戏配置摘要(UTF-8)。16 KB 够装 100+ 个包名的中文翻译。
// UI 线程写、崩溃读,无锁:撕裂至多得到旧值,与同组诊断信息一致。
char g_gameConfig[16384] = {0};

// 本局信息:开局时刻(tick)、总人数、已进行轮数。
// 开局由 setGamePhase / setGameStats 登记,切回大厅时清零。
// 写线程(对局逻辑线程 / UI 线程)与崩溃读线程无锁,撕裂至多得到一个旧值,可接受。
ULONGLONG g_gameStartTick = 0;
int g_playerCount = 0;
int g_gameRound   = 0;

thread_local void *g_luaState = nullptr;
thread_local DWORD g_luaThreadId = 0;

void appendEnv(const char *fmt, ...)
{
    size_t used = lstrlenA(g_envInfo);
    if (used >= sizeof(g_envInfo) - 1) return;
    va_list args;
    va_start(args, fmt);
    wvsprintfA(g_envInfo + used, fmt, args); // wvsprintfA 不支持 %f,够用
    va_end(args);
}

// RegGetValueW 的函数指针类型(运行时从 advapi32 取地址,免显式链接)。
typedef LONG (WINAPI *RegGetValueWFn)(HKEY, LPCWSTR, LPCWSTR, DWORD,
                                      LPDWORD, PVOID, LPDWORD);

// 读 HKLM 下一个字符串型注册表值,转 UTF-8 写入 out;失败则 out 不变。
void readRegStr(RegGetValueWFn fn, const wchar_t *subkey,
                const wchar_t *value, char *out, int outBytes)
{
    if (!fn) return;
    wchar_t wbuf[512];
    DWORD cb = sizeof(wbuf);
    if (fn(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ,
           nullptr, wbuf, &cb) == ERROR_SUCCESS)
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, outBytes,
                            nullptr, nullptr);
}

// EnumDisplayMonitors 回调:逐个打印显示器分辨率、位置、设备名。
// 设备名(\\.\DISPLAYn)与 setWindowState 登记的屏幕名同源,可对上"游戏在哪块屏"。
BOOL CALLBACK monitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
{
    int *idx = (int *)lp;
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, (LPMONITORINFO)&mi)) {
        char name8[64] = {0};
        WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name8, sizeof(name8),
                            nullptr, nullptr);
        appendEnv("显示器 %d: %d x %d @ (%d,%d) %s%s\r\n",
                  ++(*idx),
                  (int)(mi.rcMonitor.right - mi.rcMonitor.left),
                  (int)(mi.rcMonitor.bottom - mi.rcMonitor.top),
                  (int)mi.rcMonitor.left, (int)mi.rcMonitor.top, name8,
                  (mi.dwFlags & MONITORINFOF_PRIMARY) ? " [主]" : "");
    }
    return TRUE;
}

void collectEnvInfo()
{
    appendEnv("==== 环境信息 ====\r\n");
    appendEnv("Build ID: %s\r\n", QSG_BUILD_ID);

    // OS 版本:RtlGetVersion 不会像 GetVersionEx 那样在 Win8.1+ 撒谎
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW osv = {};
    osv.dwOSVersionInfoSize = sizeof(osv);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        RtlGetVersionPtr fn = (RtlGetVersionPtr)(void(*)())GetProcAddress(ntdll, "RtlGetVersion");
        if (fn && fn(&osv) == 0)
            appendEnv("Windows: %d.%d build %d\r\n",
                      (int)osv.dwMajorVersion, (int)osv.dwMinorVersion,
                      (int)osv.dwBuildNumber);
    }

    appendEnv("进程: %d 位\r\n", (int)(sizeof(void *) * 8));

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char *arch;
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64";  break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86";  break;
    default:                           arch = "其它"; break;
    }
    appendEnv("CPU 架构: %s,逻辑核心: %d\r\n",
              arch, (int)si.dwNumberOfProcessors);

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem))
        appendEnv("物理内存: %d MB(可用 %d MB)\r\n",
                  (int)(mem.ullTotalPhys / (1024 * 1024)),
                  (int)(mem.ullAvailPhys / (1024 * 1024)));

    int monIdx = 0;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, (LPARAM)&monIdx);
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        appendEnv("屏幕 DPI: %d\r\n", GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(nullptr, hdc);
    }

    wchar_t locale[64] = {0};
    if (GetUserDefaultLocaleName(locale, 64)) {
        char loc8[128] = {0};
        WideCharToMultiByte(CP_UTF8, 0, locale, -1, loc8, sizeof(loc8), nullptr, nullptr);
        appendEnv("系统区域: %s\r\n", loc8);
    }

    // ---- 硬件型号(CPU/显卡/主板)与硬盘容量 ----
    // Reg* / EnumDisplayDevices 运行时取地址,避免显式链接 advapi32/user32。
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    RegGetValueWFn regGet = advapi
        ? (RegGetValueWFn)(void(*)())GetProcAddress(advapi, "RegGetValueW") : nullptr;
    if (regGet) {
        char cpu[256] = {0};
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"ProcessorNameString", cpu, sizeof(cpu));
        if (cpu[0]) appendEnv("CPU: %s\r\n", cpu);

        char mbVendor[128] = {0}, mbProduct[128] = {0};
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BaseBoardManufacturer", mbVendor, sizeof(mbVendor));
        readRegStr(regGet, L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BaseBoardProduct", mbProduct, sizeof(mbProduct));
        if (mbVendor[0] || mbProduct[0])
            appendEnv("主板: %s %s\r\n", mbVendor, mbProduct);
    }

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    typedef BOOL (WINAPI *EnumDisplayDevicesWFn)(LPCWSTR, DWORD,
                                                 PDISPLAY_DEVICEW, DWORD);
    EnumDisplayDevicesWFn enumDD = user32
        ? (EnumDisplayDevicesWFn)(void(*)())GetProcAddress(user32, "EnumDisplayDevicesW")
        : nullptr;
    if (enumDD) {
        DISPLAY_DEVICEW dd;
        char lastGpu[256] = {0};
        for (DWORD i = 0; ; ++i) {
            ZeroMemory(&dd, sizeof(dd));
            dd.cb = sizeof(dd);
            if (!enumDD(nullptr, i, &dd, 0)) break;
            if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue; // 跳过未启用/镜像驱动
            char gpu[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, dd.DeviceString, -1,
                                gpu, sizeof(gpu), nullptr, nullptr);
            // 同一显卡接多块显示器会重复枚举,去掉相邻重复
            if (gpu[0] && lstrcmpA(gpu, lastGpu) != 0) {
                appendEnv("显卡: %s\r\n", gpu);
                lstrcpynA(lastGpu, gpu, sizeof(lastGpu));
            }
        }
    }

    // 硬盘容量:游戏所在盘(传 nullptr 即当前工作目录所在卷)
    ULARGE_INTEGER diskAvail, diskTotal, diskTotalFree;
    if (GetDiskFreeSpaceExW(nullptr, &diskAvail, &diskTotal, &diskTotalFree))
        appendEnv("硬盘(游戏所在盘): 总 %d GB / 可用 %d GB\r\n",
                  (int)(diskTotal.QuadPart / (1024ULL * 1024 * 1024)),
                  (int)(diskTotalFree.QuadPart / (1024ULL * 1024 * 1024)));

    // 程序路径、工作目录、命令行
    wchar_t pathw[MAX_PATH] = {0};
    char path8[MAX_PATH * 3] = {0};
    if (GetModuleFileNameW(nullptr, pathw, MAX_PATH)) {
        WideCharToMultiByte(CP_UTF8, 0, pathw, -1, path8, sizeof(path8),
                            nullptr, nullptr);
        appendEnv("程序路径: %s\r\n", path8);
    }
    if (GetCurrentDirectoryW(MAX_PATH, pathw)) {
        WideCharToMultiByte(CP_UTF8, 0, pathw, -1, path8, sizeof(path8),
                            nullptr, nullptr);
        appendEnv("工作目录: %s\r\n", path8);
    }
    char cmd8[1024] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, GetCommandLineW(), -1,
                            cmd8, sizeof(cmd8), nullptr, nullptr))
        appendEnv("命令行: %s\r\n", cmd8);
}

// 把崩溃文件名前缀(不含扩展名)写入 out(宽字符)。
// 例:dmp\crash-20260515-203045-20260420-a1b2c3d
void buildPrefix(wchar_t *out, size_t cch)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(out, L"dmp\\crash-%04d%02d%02d-%02d%02d%02d-%S-%S",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond,
              g_version,         // 游戏版本号,由 setVersion 在 Engine 构造后登记
              QSG_BUILD_ID);
    (void)cch;
}

// 写 minidump 到 <prefix>.dmp。pointers 为 nullptr 时用当前上下文合成。
bool writeMiniDump(const wchar_t *prefix, EXCEPTION_POINTERS *pointers)
{
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.dmp", prefix);

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    // abort/terminate 等路径没有 EXCEPTION_POINTERS,合成一份。
    EXCEPTION_RECORD record;
    CONTEXT context;
    EXCEPTION_POINTERS synthesized;
    if (!pointers) {
        ZeroMemory(&record, sizeof(record));
        ZeroMemory(&context, sizeof(context));
        RtlCaptureContext(&context);
        record.ExceptionCode = 0xE0000001; // 自定义"非 SEH 崩溃"码
        synthesized.ExceptionRecord = &record;
        synthesized.ContextRecord = &context;
        pointers = &synthesized;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pointers;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                hFile, MiniDumpNormal, &mei, nullptr, nullptr);
    CloseHandle(hFile);
    return ok != FALSE;
}

// 读磁盘上 PE 文件(exe/dll)头里的链接时首选基址 ImageBase。
// 关键:不能从内存里映射模块的 PE 头取这个值 —— 开了 ASLR 后,加载器会把内存
// 里的 OptionalHeader.ImageBase 改写成重定位后的运行时随机基址;只有磁盘上的
// 文件不被改动,读它才拿得到 addr2line 要的链接基址。失败返回 0。
ULONGLONG diskImageBase(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return 0;

    ULONGLONG base = 0;
    IMAGE_DOS_HEADER dos;
    DWORD got = 0;
    if (ReadFile(f, &dos, sizeof(dos), &got, nullptr) && got == sizeof(dos)
        && dos.e_magic == IMAGE_DOS_SIGNATURE
        && SetFilePointer(f, dos.e_lfanew, nullptr, FILE_BEGIN)
               != INVALID_SET_FILE_POINTER) {
        IMAGE_NT_HEADERS nt;
        if (ReadFile(f, &nt, sizeof(nt), &got, nullptr) && got == sizeof(nt)
            && nt.Signature == IMAGE_NT_SIGNATURE)
            base = nt.OptionalHeader.ImageBase;
    }
    CloseHandle(f);
    return base;
}

// 写崩溃摘要 txt 到 <prefix>.txt。
void writeSummary(const wchar_t *prefix, const char *reason,
                  EXCEPTION_POINTERS *pointers)
{
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.txt", prefix);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    char buf[2048];
    DWORD written = 0;

    DWORD tid = GetCurrentThreadId();
    const char *threadKind = (tid == g_mainThreadId) ? "主线程" : "后台线程";
    unsigned upSec = (unsigned)((tickCount64() - g_startTick) / 1000);
    wsprintfA(buf, "==== 崩溃摘要 ====\r\n崩溃类型: %s\r\n游戏版本: %s\r\n"
                   "崩溃线程: %u(%s)\r\n进程已运行: %u 秒\r\n",
              reason, g_version, (unsigned)tid, threadKind, upSec);
    WriteFile(h, buf, lstrlenA(buf), &written, nullptr);

    // 崩溃时进程内存占用(K32GetProcessMemoryInfo 运行时取地址,免链接 psapi)
    typedef BOOL (WINAPI *GetProcMemFn)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);
    GetProcMemFn memFn = (GetProcMemFn)(void(*)())GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    if (memFn) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        ZeroMemory(&pmc, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        if (memFn(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc,
                  sizeof(pmc))) {
            wsprintfA(buf, "进程内存: 工作集 %d MB / 私有 %d MB\r\n",
                      (int)(pmc.WorkingSetSize / (1024 * 1024)),
                      (int)(pmc.PrivateUsage / (1024 * 1024)));
            WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        }
    }

    // 游戏阶段与窗口状态(崩溃那一刻,由 UI 线程登记)
    const char *phaseStr;
    switch (g_gamePhase) {
    case 1:  phaseStr = "对局中(玩家存活)";              break;
    case 2:  phaseStr = "对局中(玩家已阵亡,AI 接管快进)"; break;
    case 3:  phaseStr = "录像回放";                        break;
    default: phaseStr = "大厅 / 未开局";                   break;
    }
    wsprintfA(buf, "游戏阶段: %s\r\n", phaseStr);
    WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    if (g_windowState[0]) {
        wsprintfA(buf, "游戏窗口: %s\r\n", g_windowState);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    }

    // 本局信息(开局时刻/人数/轮数,见 setGamePhase / setGameStats)。
    // g_gameStartTick 非 0 即说明崩溃时正在对局/回放中。
    if (g_gameStartTick != 0) {
        unsigned gameSec = (unsigned)((tickCount64() - g_gameStartTick) / 1000);
        if (g_playerCount > 0)
            wsprintfA(buf, "本局时长: %u 秒\r\n本局人数: %d 人\r\n"
                           "已进行轮数: 第 %d 轮\r\n",
                      gameSec, g_playerCount, g_gameRound);
        else // 回放等场景拿不到人数/轮数,只给时长
            wsprintfA(buf, "本局时长: %u 秒\r\n", gameSec);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
    }

    if (pointers && pointers->ExceptionRecord) {
        void *addr = pointers->ExceptionRecord->ExceptionAddress;
        wsprintfA(buf, "异常码: 0x%08X\r\n崩溃地址: 0x%p\r\n",
                  (unsigned)pointers->ExceptionRecord->ExceptionCode, addr);
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);

        // 解析崩溃地址所属模块、相对偏移、符号化用的虚拟地址
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)addr, &mod) && mod) {
            wchar_t modPath[MAX_PATH] = {0};
            GetModuleFileNameW(mod, modPath, MAX_PATH);
            const wchar_t *modName = wcsrchr(modPath, L'\\');
            modName = modName ? modName + 1 : modPath;
            uintptr_t offset = (uintptr_t)addr - (uintptr_t)mod;

            // addr2line 要的是"PE 链接时首选基址 + RVA"的虚拟地址 —— 不是运行
            // 时地址(模块可能被 ASLR 重定位过),也不是裸 RVA。首选基址必须从
            // 磁盘上的模块文件读:内存里 PE 头的 ImageBase 已被加载器改成运行
            // 时随机基址。读盘失败时退回内存基址(等价修复前的老行为 —— 未开
            // ASLR 时仍正确,Qt5 的 mingw730 构建即此情形)。
            ULONGLONG linkBase = diskImageBase(modPath);
            if (linkBase == 0)
                linkBase = (ULONGLONG)mod;
            ULONGLONG va = linkBase + offset;

            wsprintfA(buf, "崩溃模块: %S\r\n模块内偏移: 0x%IX\r\n"
                           "符号化地址: 0x%I64X\r\n"
                           "(符号化: addr2line -e <模块>.debug -f -C -i 0x%I64X)\r\n",
                      modName, offset, va, va);
            WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        }
    } else {
        // 没有异常上下文 —— abort/terminate/SIGABRT 类,或玩家手动上报卡死。
        // 把 reason 带上,既准确又免去再加分支。
        char nabuf[256];
        wsprintfA(nabuf, "异常: 无(%s)\r\n", reason);
        WriteFile(h, nabuf, lstrlenA(nabuf), &written, nullptr);
    }

    WriteFile(h, "\r\n", 2, &written, nullptr);
    WriteFile(h, g_envInfo, lstrlenA(g_envInfo), &written, nullptr);

    // 游戏配置摘要(Qt 侧由 stashGameConfigForCrash 预暂存,见 settings.cpp)。
    // 不为空才写出标题,极早期崩溃 / 未初始化时段静默跳过、避免出现空标题。
    if (g_gameConfig[0]) {
        const char *hdr = "\r\n==== 游戏配置 ====\r\n";
        WriteFile(h, hdr, lstrlenA(hdr), &written, nullptr);
        WriteFile(h, g_gameConfig, lstrlenA(g_gameConfig), &written, nullptr);
        WriteFile(h, "\r\n", 2, &written, nullptr);
    }
    CloseHandle(h);
}

// 把当前工作目录的 config.ini 复制到 dmp\<prefix>-config.ini,供事后排查。
// 失败静默(老用户首次启动 config.ini 可能尚未生成;占用、
// 权限失败也不影响主流程)。crashhandler 始终在工作目录 = 部署目录运行,
// config.ini 即 Settings 实际读写的那份。
void copyConfigIni(const wchar_t *prefix)
{
    wchar_t dst[MAX_PATH];
    wsprintfW(dst, L"%s-config.ini", prefix);
    CopyFileW(L"config.ini", dst, FALSE);
}

// 把崩溃线程当前的 Lua 调用栈(.lua 文件名 + 行号)写进已打开的摘要文件 h。
// 为什么需要它:minidump / 原生栈回溯只能看到 C 调用栈,而所有 Lua 函数都由
// 解释器 luaV_execute 这一个 C 函数解释执行 —— "执行到哪个 .lua 第几行"存在
// Lua 自己的调用信息链(lua_State 的 CallInfo)里,不在 C 栈上。这里调 Lua
// 调试接口把它读出来。lua_getstack / lua_getinfo 只读遍历,不分配、不执行 Lua。
void writeLuaStack(HANDLE h)
{
    lua_State *L = (lua_State *)g_luaState;
    char buf[600];
    DWORD written = 0;

    const char *hdr =
        "\r\n==== Lua 调用栈(崩溃线程)====\r\n"
        "(原生栈只到 Lua 解释器为止;以下行号系崩溃时回查 Lua 调试信息得到)\r\n";
    WriteFile(h, hdr, lstrlenA(hdr), &written, nullptr);

    lua_Debug ar;
    int shown = 0;
    for (int level = 0; level < 100 && lua_getstack(L, level, &ar); ++level) {
        if (!lua_getinfo(L, "Sln", &ar))
            break;
        // 函数名:Lua 仅在"能从调用处推断名字"时给得出 —— 经 pcall / C API
        // 调用的函数没有名字,此时退而给出函数定义所在行,照样能定位。
        if (ar.currentline >= 0) { // Lua 帧:有源文件与行号
            if (ar.name && ar.name[0])
                wsprintfA(buf, "  #%d  %s:%d  函数 %s\r\n",
                          level, ar.short_src, ar.currentline, ar.name);
            else
                wsprintfA(buf, "  #%d  %s:%d  (函数定义于第 %d 行)\r\n",
                          level, ar.short_src, ar.currentline, ar.linedefined);
        } else {                   // C 帧(SWIG wrapper / Lua 库函数,原生栈里已有)
            const char *name = (ar.name && ar.name[0]) ? ar.name : "?";
            wsprintfA(buf, "  #%d  [C]  %s\r\n", level, name);
        }
        WriteFile(h, buf, lstrlenA(buf), &written, nullptr);
        ++shown;
    }
    if (shown == 0) {
        const char *none = "  (空 —— 崩溃时不在 Lua 执行中)\r\n";
        WriteFile(h, none, lstrlenA(none), &written, nullptr);
    }
}

// 把 Lua 调用栈追加到摘要 txt 末尾。仅在崩溃线程就是登记 Lua 状态机的那个
// 线程时才做(见 g_luaState 注释)。
void appendLuaStack(const wchar_t *prefix)
{
    if (!g_luaState || GetCurrentThreadId() != g_luaThreadId)
        return;

    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s.txt", prefix);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    writeLuaStack(h);
    CloseHandle(h);
}

// 所有捕获路径最终汇入这里。pointers 可能为 nullptr(abort/terminate 等路径)。
void handleCrash(const char *reason, EXCEPTION_POINTERS *pointers)
{
    // 已进入正常关闭流程:退出清理阶段的崩溃不上报(见 g_shuttingDown 注释)。
    if (g_shuttingDown)
        return;

    if (InterlockedExchange(&g_handling, 1) != 0)
        return;

    CreateDirectoryW(L"dmp", nullptr);

    wchar_t prefix[MAX_PATH];
    buildPrefix(prefix, MAX_PATH);

    writeMiniDump(prefix, pointers);
    writeSummary(prefix, reason, pointers);
    copyConfigIni(prefix);

    // Lua 调用栈最后追加 —— 回查 Lua 调试信息有极小的二次崩溃风险(崩溃可能
    // 已损坏 Lua 内存)。放在 minidump、摘要都落地之后,万一这步再崩,
    // 前面成果不受影响。
    appendLuaStack(prefix);
}

LONG WINAPI sehFilter(EXCEPTION_POINTERS *pointers)
{
    handleCrash("SEH", pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
    handleCrash("terminate", nullptr);
    _exit(3);
}

void sigabrtHandler(int)
{
    handleCrash("SIGABRT", nullptr);
    _exit(3);
}

} // namespace

namespace CrashHandler {

void install()
{
    g_mainThreadId = GetCurrentThreadId();
    g_startTick = tickCount64();
    SetUnhandledExceptionFilter(sehFilter);
    std::set_terminate(terminateHandler);
    signal(SIGABRT, sigabrtHandler);
    // 不调用 _set_abort_behavior:MinGW 的 msvcrt 导入库无此符号;
    // 且 sigabrtHandler 会 _exit,系统 abort 对话框本就来不及弹出。
    collectEnvInfo();
}

void setVersion(const char *version)
{
    if (version && version[0])
        lstrcpynA(g_version, version, sizeof(g_version));
}

void beginShutdown()
{
    InterlockedExchange(&g_shuttingDown, 1);
}

void setLiveRecordPath(const wchar_t *path)
{
    if (path && path[0])
        lstrcpynW(g_liveRecord, path, MAX_PATH);
    else
        g_liveRecord[0] = 0;
}

void setGamePhase(GamePhase phase)
{
    g_gamePhase = (int)phase;
    if (phase == PhaseLobby) {
        // 回大厅:本局信息作废,清零 —— 否则下次崩在大厅会带上一局的残留
        g_gameStartTick = 0;
        g_playerCount = 0;
        g_gameRound = 0;
    } else if (g_gameStartTick == 0) {
        // 首次进入对局 / 回放,记下开局时刻,供崩溃时算本局时长
        g_gameStartTick = tickCount64();
    }
}

void setGameStats(int playerCount, int round)
{
    if (playerCount > 0)
        g_playerCount = playerCount;
    if (round >= 0)
        g_gameRound = round;
}

void setLuaState(void *L)
{
    g_luaState = L;
    g_luaThreadId = L ? GetCurrentThreadId() : 0;
}

void setWindowState(int x, int y, int w, int h, const wchar_t *screenName)
{
    char name8[64] = {0};
    if (screenName && screenName[0])
        WideCharToMultiByte(CP_UTF8, 0, screenName, -1, name8, sizeof(name8),
                            nullptr, nullptr);
    wsprintfA(g_windowState, "%d x %d @ (%d,%d)%s%s",
              w, h, x, y, name8[0] ? "  屏幕: " : "", name8);
}

void setGameConfig(const char *utf8)
{
    if (utf8 && utf8[0])
        lstrcpynA(g_gameConfig, utf8, sizeof(g_gameConfig));
    else
        g_gameConfig[0] = 0;
}

void reportHang()
{
    // 玩家手动上报路径,与 handleCrash 同构,但:
    //   - 不锁全局 g_handling —— 后续真崩溃仍要能正常报告。用本地 once
    //     标志只防本函数自己被快速点出重入,结束前清掉。
    //   - 不退出进程 —— 卡死处理完毕,玩家自行关窗口。
    //   - 没有异常上下文,writeMiniDump 已有 nullptr 合成逻辑。
    // 设计稿:docs/specs/2026-05-20-hang-report-and-crash-config-design.md
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0)
        return;

    if (!g_shuttingDown) {
        CreateDirectoryW(L"dmp", nullptr);

        wchar_t prefix[MAX_PATH];
        buildPrefix(prefix, MAX_PATH);

        writeMiniDump(prefix, nullptr);
        writeSummary(prefix, "卡死(玩家手动上报)", nullptr);
        copyConfigIni(prefix);
    }

    InterlockedExchange(&once, 0);
}

const char *buildId()
{
    return QSG_BUILD_ID;
}

void selfTest(const char *type)
{
    if (lstrcmpA(type, "av") == 0) {
        volatile int *p = nullptr;
        *p = 1;
    } else if (lstrcmpA(type, "abort") == 0) {
        abort();
    } else if (lstrcmpA(type, "throw") == 0) {
        throw std::runtime_error("crashtest: uncaught exception");
    }
}

} // namespace CrashHandler

#else  // 非 Windows 或未启用 crash handler:空实现

namespace CrashHandler {
void install() {}
void setVersion(const char *) {}
void beginShutdown() {}
void setLiveRecordPath(const wchar_t *) {}
void setGamePhase(GamePhase) {}
void setGameStats(int, int) {}
void setLuaState(void *) {}
void setWindowState(int, int, int, int, const wchar_t *) {}
void setGameConfig(const char *) {}
void reportHang() {}
const char *buildId() { return "unknown"; }
void selfTest(const char *) {}
}

#endif
