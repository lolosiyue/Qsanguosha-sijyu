// Compile the unchanged native CLI with main=qsan_fixture_main for this target.
// Calling a small C export avoids Emscripten's process-exit/callMain semantics.
#include <emscripten/emscripten.h>
#include <cstdio>

int qsan_fixture_main(int argc, char **argv);

extern "C" EMSCRIPTEN_KEEPALIVE int qsan_run_fixture()
{
    // Engine/bootstrap globals are not a multi-session API. Use a fresh module
    // (and a fresh host process in the parity harness) for every fixture.
    static bool called = false;
    if (called) {
        std::fputs("fixture WASM instance is single-use\n", stderr);
        return 64;
    }
    called = true;
    char program[] = "qsanguosha_rules_fixture_runner";
    char fixtureOption[] = "--fixture";
    char fixture[] = "/work/input.json";
    char outputOption[] = "--output";
    char output[] = "/work/output.json";
    char assetsOption[] = "--asset-root";
    char assets[] = "/assets";
    char *argv[] = {program, fixtureOption, fixture, outputOption, output,
                    assetsOption, assets, nullptr};
    return qsan_fixture_main(7, argv);
}
