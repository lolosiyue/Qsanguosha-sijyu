#ifndef QSAN_XP_LUA_PATHS_TEST_H
#define QSAN_XP_LUA_PATHS_TEST_H

#include "xp-lua-paths.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryDir>

inline int runXpLuaPathsTest()
{
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString assets = directory.path() + "/assets";
    const QString profile = directory.path() + "/profile";
    auto write = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    if (!write(assets + "/lua/config.lua", "-- fixture")
        || !write(assets + "/lua/sanguosha.lua", "-- fixture")
        || !write(assets + "/defaults.txt", "seed")
        || !write(assets + "/update.txt", "seed")
        || !write(assets + "/script.lua", "return 41")) return 2;
    QFile::setPermissions(assets + "/defaults.txt", QFile::ReadOwner);
    qsanXpSetEnvironment("QSAN_USER_DATA_ROOT", profile);
    QString error;
    if (!QSanRuntimePaths::resolve({"test", "--asset-root", assets}, &error)) return 3;
    lua_State *state = luaL_newstate();
    if (!state) return 4;
    luaL_openlibs(state);
    bool passed = XpLuaPaths::install(state);
    const QByteArray absolute = QFile::encodeName(assets + "/defaults.txt");
    lua_pushlstring(state, absolute.constData(), size_t(absolute.size()));
    lua_setglobal(state, "absolute_default");
    const char *script = R"lua(
local function read(name)
    local f = assert(io.open(name, "r")); local s = f:read("*a"); f:close(); return s
end
assert(read("defaults.txt") == "seed")
local f = assert(io.open("defaults.txt", "a")); f:write("+append"); f:close()
assert(read(absolute_default) == "seed+append")
f = assert(io.open("update.txt", "r+")); f:write("S"); f:close()
assert(read("update.txt") == "Seed")
f = assert(io.open("nested/new.txt", "w")); f:write("line\n"); f:close()
assert(io.lines("nested/new.txt")() == "line")
io.input("nested/new.txt"); assert(io.read("*l") == "line"); io.input():close()
io.output("nested/output.txt"); io.write("output"); io.output():close()
assert(read("nested/output.txt") == "output")
assert(os.rename("nested/output.txt", "nested/renamed.txt"))
assert(os.remove("nested/renamed.txt"))
assert(io.open("missing.txt", "r") == nil)
assert(dofile("script.lua") == 41)
f = assert(io.open("script.lua", "w")); f:write("return 42"); f:close()
assert(assert(loadfile("script.lua"))() == 42)
assert(dofile("script.lua") == 42)
-- Removing a bundled file may not delete the read-only installation.
assert(os.remove("update.txt"))
assert(os.remove("update.txt") == nil)
)lua";
    if (passed) passed = luaL_dostring(state, script) == LUA_OK;
    if (!passed) qCritical("XP Lua path regression: %s", lua_tostring(state, -1));
    lua_close(state);
    for (const QString &name : {QString("defaults.txt"), QString("update.txt")}) {
        QFile original(assets + '/' + name);
        passed = original.open(QIODevice::ReadOnly) && original.readAll() == "seed" && passed;
    }
    passed = !QFileInfo::exists(assets + "/nested") && passed;
    // Verify a fresh Lua state sees the same user data rather than bundled data.
    state = luaL_newstate();
    if (state) {
        luaL_openlibs(state);
        passed = XpLuaPaths::install(state) && luaL_dostring(state,
            "local f=assert(io.open('defaults.txt')); assert(f:read('*a')=='seed+append'); f:close()") == LUA_OK && passed;
        lua_close(state);
    } else passed = false;
    QDir::setCurrent(QCoreApplication::applicationDirPath());
    QFile::setPermissions(assets + "/defaults.txt", QFile::ReadOwner | QFile::WriteOwner);
    qInfo("XP Lua read-only paths: %s", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

#endif
