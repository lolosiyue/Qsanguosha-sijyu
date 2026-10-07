#include "lua.hpp"
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    lua_State *state = luaL_newstate();
    if (!state) return 2;
    luaL_openlibs(state);
    const int result = luaL_dofile(state, argv[1]);
    if (result != LUA_OK)
        std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return result == LUA_OK ? 0 : 1;
}
