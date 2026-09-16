#include <QList>
#include "util.h"

#include "lua.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#ifndef QSAN_PACKAGE_SOURCE_ROOT
#error QSAN_PACKAGE_SOURCE_ROOT must point to the source tree
#endif

namespace {
bool writeFile(const QString &root, const QString &relative, const QByteArray &bytes)
{
    const QString path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool evaluate(lua_State *state, const char *script)
{
    if (luaL_dostring(state, script) == LUA_OK) {
        const bool result = lua_toboolean(state, -1);
        lua_pop(state, 1);
        return result;
    }
    QTextStream(stderr) << "package loader Lua fixture failed: "
                        << lua_tostring(state, -1) << '\n';
    lua_pop(state, 1);
    return false;
}

bool installMocks(lua_State *state)
{
    static const char script[] = R"lua(
        package_execution_order = {}
        added_package_order = {}
        loaded_translations = {}
        local config = {
            package_lua = {
                "packages/one/lua/helper.lua",
                "packages/two/lua/helper.lua",
                "packages/one/lua/ai/server-only.lua"
            },
            extension_ids = {"sijyu", "second"},
            extension_names = {
                "packages/one/lua/sijyu.lua",
                "packages/two/lua/second.lua"
            },
            package_lang = {"packages/one/translation/en.lua"}
        }
        local function skillList()
            local result = {}
            function result:append(value) self[#self + 1] = value end
            return result
        end
        sgs = {
            GetConfigList = function(key) return config[key] or {} end,
            GetConfig = function(_, default) return default end,
            GetFileNames = function() return {} end,
            SkillList = skillList,
            qlist = ipairs,
            LoadTranslationTable = function(values)
                loaded_translations[#loaded_translations + 1] = values
            end,
            SetConfig = function() end,
            Sanguosha = {
                getSkill = function() return true end,
                addPackage = function(_, item)
                    added_package_order[#added_package_order + 1] = item:objectName()
                end,
                addSkills = function() end,
                isGameLuaRuntime = function() return false end,
                isLuaDefinitionsLoaded = function() return false end,
                finishLuaDefinitions = function() end
            }
        }
        _test_original_require = require
        return true
    )lua";
    return evaluate(state, script);
}

bool setupRuntime(const QString &root)
{
    const QString sourceRoot = QString::fromUtf8(QSAN_PACKAGE_SOURCE_ROOT);
    const QString productionLoader = QDir(sourceRoot).filePath(QStringLiteral("lua/sanguosha.lua"));
    const QString runtimeLoader = QDir(root).filePath(QStringLiteral("lua/sanguosha.lua"));
    return QDir().mkpath(QDir(root).filePath(QStringLiteral("lua")))
        && QFile::copy(productionLoader, runtimeLoader)
        && writeFile(root, QStringLiteral("lua/utilities.lua"), QByteArray())
        && writeFile(root, QStringLiteral("lua/sgs_ex.lua"), QByteArray())
        && writeFile(root, QStringLiteral("packages/one/lua/helper.lua"),
                     QByteArrayLiteral("return {name = 'one'}\n"))
        && writeFile(root, QStringLiteral("packages/two/lua/helper.lua"),
                     QByteArrayLiteral("return {name = 'two'}\n"))
        && writeFile(root, QStringLiteral("packages/one/lua/sijyu.lua"), QByteArrayLiteral(R"lua(
            local helper = require("helper")
            package_execution_order[#package_execution_order + 1] = helper.name
            module("extensions.sijyu", package.seeall)
            extension = {objectName = function() return helper.name end}
        )lua"))
        && writeFile(root, QStringLiteral("packages/two/lua/second.lua"), QByteArrayLiteral(R"lua(
            local helper = require("helper")
            package_execution_order[#package_execution_order + 1] = helper.name
            return {extension = {objectName = function() return helper.name end}}
        )lua"))
        && writeFile(root, QStringLiteral("packages/one/translation/en.lua"),
                     QByteArrayLiteral("return {package_probe = 'translated'}\n"))
        // The exact legacy file is intentionally present. The migrated module alias must resolve
        // from package.loaded without evaluating this stale script.
        && writeFile(root, QStringLiteral("extensions/sijyu.lua"),
                     QByteArrayLiteral("error('stale legacy extension executed')\n"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid() || !setupRuntime(temporary.path()))
        return 1;

    lua_State *state = CreateLuaState();
    if (!state)
        return 2;
    const QString originalDirectory = QDir::currentPath();
    const bool changedDirectory = QDir::setCurrent(temporary.path());
    const bool mocksReady = changedDirectory && installMocks(state);
    const bool loaderReady = mocksReady && evaluate(state, "dofile('lua/sanguosha.lua'); return true");
    const bool passed = loaderReady && evaluate(state, R"lua(
        local legacy = require("extensions.sijyu")
        local one = sgs.RequirePackage("one", "helper")
        local two = sgs.RequirePackage("two", "helper")
        local missingAi = pcall(require, "packages.one.lua.ai.server-only")
        return require == _test_original_require
            and not missingAi
            and package_execution_order[1] == "one"
            and package_execution_order[2] == "two"
            and added_package_order[1] == "one"
            and added_package_order[2] == "two"
            and one.name == "one" and two.name == "two"
            and legacy == package.loaded["packages.one.lua.sijyu"]
            and legacy == package.loaded["extensions.sijyu"]
            and legacy.extension:objectName() == "one"
            and #loaded_translations == 1
            and loaded_translations[1].package_probe == "translated"
    )lua");
    if (changedDirectory)
        QDir::setCurrent(originalDirectory);
    lua_close(state);
    if (!passed)
        QTextStream(stderr) << "production Lua package loader contract failed\n";
    return passed ? 0 : 3;
}
