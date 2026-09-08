#ifndef QSAN_XP_LUA_PATHS_H
#define QSAN_XP_LUA_PATHS_H

#include "lua.hpp"
#include "runtime-paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace XpLuaPaths {

// Legacy extensions use relative io paths while CWD remains the asset root for
// images and scripts. Overlay only those paths; explicit external paths retain
// their normal meaning. Never redirect the process CWD to a writable directory.
inline int resolve(lua_State *state)
{
    const QString path = QString::fromLocal8Bit(luaL_checkstring(state, 1));
    const QByteArray mode(luaL_optstring(state, 2, "r"));
    const QString assets = QSanRuntimePaths::assetRoot();
    if (assets.isEmpty() || QSanRuntimePaths::userDataRoot().isEmpty()) {
        lua_pushvalue(state, 1);
        return 1;
    }
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(assets).relativeFilePath(absolute);
    if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../")) {
        lua_pushvalue(state, 1);
        return 1;
    }

    const bool writing = mode.contains('w') || mode.contains('a')
        || mode.contains('+') || mode == "remove";
    QString mapped = writing ? QSanRuntimePaths::userDataPath(relative)
                             : QSanRuntimePaths::readablePath(relative);
    // Append/update must preserve bundled defaults on the first write. Plain w
    // deliberately truncates; reads use the overlay before the bundled file.
    if (writing && (mode.contains('a') || mode.contains('+')) && !mode.startsWith('w')
        && !QFileInfo::exists(mapped) && QFileInfo(absolute).isFile()
        && !QFile::copy(absolute, mapped)) {
        lua_pushnil(state);
        lua_pushliteral(state, "unable to copy bundled data into the XP user profile");
        return 2;
    }
    if (writing && QFileInfo::exists(mapped)) {
        // QFile::copy preserves the source permissions, including CD read-only.
        QFile::setPermissions(mapped, QFile::permissions(mapped) | QFile::WriteOwner);
    }
    const QByteArray encoded = QFile::encodeName(mapped);
    lua_pushlstring(state, encoded.constData(), size_t(encoded.size()));
    return 1;
}

inline bool install(lua_State *state)
{
    lua_pushcfunction(state, resolve);
    lua_setglobal(state, "__qsan_xp_io_path");
    const char *script = R"lua(
do
    local path = __qsan_xp_io_path
    __qsan_xp_io_path = nil
    local open, input, output, lines = io.open, io.input, io.output, io.lines
    local remove, rename = os.remove, os.rename
    local load, run = loadfile, dofile
    io.open = function(name, mode)
        local mapped, message = path(name, mode or "r")
        if not mapped then return nil, message end
        return open(mapped, mode)
    end
    io.input = function(file)
        if type(file) == "string" then file = assert(path(file, "r")) end
        return input(file)
    end
    io.output = function(file)
        if type(file) == "string" then file = assert(path(file, "w")) end
        return output(file)
    end
    io.lines = function(file, ...)
        if file ~= nil then file = assert(path(file, "r")) end
        return lines(file, ...)
    end
    os.remove = function(file) return remove(assert(path(file, "remove"))) end
    os.rename = function(from, to)
        return rename(assert(path(from, "remove")), assert(path(to, "remove")))
    end
    loadfile = function(file, ...)
        if file ~= nil then file = assert(path(file, "r")) end
        return load(file, ...)
    end
    dofile = function(file)
        if file ~= nil then file = assert(path(file, "r")) end
        return run(file)
    end
end
)lua";
    return luaL_dostring(state, script) == LUA_OK;
}

} // namespace XpLuaPaths

#endif
