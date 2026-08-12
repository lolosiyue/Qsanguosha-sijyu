#ifndef QSG_CRASHHANDLER_H
#define QSG_CRASHHANDLER_H

// 崩溃报告器对外接口。实现仅在 Windows + 定义了 QSG_CRASH_HANDLER 时生效;
// 其他构建下所有函数为空实现(见 crashhandler.cpp),调用方无需加条件编译。
namespace CrashHandler {

// 在 main() 中尽早调用一次:安装三类崩溃捕获器并缓存环境信息。
void install();

// 由 main() 在 Engine 构造后调用,登记游戏版本号 —— install() 阶段
// Engine 尚未就绪拿不到版本号,只能稍后补登记。内部会拷贝字符串。
void setVersion(const char *version);

// 由 main() 在 qApp->exec() 返回后调用,标记进程进入正常关闭流程。
// 此后退出清理阶段(Lua 关闭、__gc 终结器经 SWIG 回调 C++ 析构等)抛出的
// 崩溃不再上报 —— 玩家已主动退出,这类崩溃无损失、非游戏内闪退。
void beginShutdown();

// 由 Recorder 调用,登记当前对局录像文件的绝对路径(宽字符)。
// 传 nullptr 清除登记(对局结束/不在对局中)。内部会拷贝字符串。
void setLiveRecordPath(const wchar_t *path);

// 游戏阶段(粗粒度)—— 崩溃摘要据此说明玩家当时在做什么。
enum GamePhase {
    PhaseLobby = 0,            // 大厅 / 未开局
    PhasePlaying = 1,          // 对局中,玩家存活
    PhaseDeadFastForward = 2,  // 对局中,玩家已阵亡(AI 接管,快进)
    PhaseReplay = 3            // 录像回放
};

// 由 UI 在游戏阶段变化时调用,登记当前阶段。
// 进入对局/回放时内部顺带记下开局时刻(用于算本局时长);
// 切回大厅(PhaseLobby)时把本局时长/人数/轮数一并清零。
void setGamePhase(GamePhase phase);

// 由 GameRule 在开局 / 每轮开始时调用,登记本局总人数与已进行轮数。
// 崩溃摘要据此说明"几人局、崩在第几轮"。playerCount<=0 时不更新人数,
// round<0 时不更新轮数(便于只更新其一)。回大厅时由 setGamePhase 清零。
void setGameStats(int playerCount, int round);

// 由 RoomThread 在 run() 开头调用,登记本对局逻辑线程的 Lua 状态机
// (传 void* 避免把 lua_State 类型泄进这个被广泛 include 的头文件)。
// 崩溃时若正好崩在该线程,会把 Lua 调用栈(.lua 文件名+行号)追加进
// 崩溃摘要 —— 原生栈回溯只能到 Lua 解释器(luaV_execute)为止,Lua 自己
// "执行到哪个 .lua 第几行"存在解释器的数据结构里、不在 C 调用栈上。
void setLuaState(void *L);

// 由主窗口在 resize/move 时调用,登记崩溃那一刻的窗口位置/大小/所在屏幕。
// screenName 为屏幕设备名(如 \\.\DISPLAY1),可为 nullptr。内部只存格式化结果。
void setWindowState(int x, int y, int w, int h, const wchar_t *screenName);

// 由 Qt 侧在「Qt 状态正常」时刻调用(Settings::init() 末尾、ServerDialog
// 确定 OK 末尾),把已格式化好的 UTF-8 配置摘要塞进固定缓冲区(16 KB)。
// 崩溃/卡死写摘要时原样吐出 —— 崩溃上下文里只用 Win32 API,不再触碰
// Engine/Config 避免二次崩溃。utf8 为 nullptr 或空串时清除暂存。
void setGameConfig(const char *utf8);

// 玩家从「帮助」菜单手动触发卡死上报。生成与崩溃同款的全线程 minidump
// + 摘要 + config.ini,拉起 crashreporter.exe。与 handleCrash 区别:进
// 程不退出(卡死处理完毕,玩家自行关窗口),不锁全局 g_handling(后续
// 真崩溃仍要能正常上报),用本地 once 防本函数重入。可被反复点击,每
// 次都生成新一份报告。
void reportHang();

// 取本次构建的 build id 字符串(QSG_BUILD_ID 宏值,或缺失时的 "unknown")。
// 主要供 serverlog 等模块写诊断 banner 用。返回的 C 字符串生命周期永久。
const char *buildId();

// 触发一次指定类型的崩溃,仅供 -crashtest 验证使用。
// type: "av"=访问违例 "abort"=abort() "throw"=未捕获异常
void selfTest(const char *type);

} // namespace CrashHandler

#endif // QSG_CRASHHANDLER_H
