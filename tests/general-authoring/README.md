# Playable general authoring tests

This is a standalone regression project for the authoring document, provider
transport and dialog. It links Qt Core, Network and Widgets and compiles the
repository's Lua parser/compiler sources with the bounded syntax checker. The
checker tests compile candidate text only; the test harness does not open Lua
libraries or evaluate a candidate. A top-level infinite loop is included to
catch accidental evaluation under CTest's timeout.

Configure and run from the repository root:

```sh
cmake -S tests/general-authoring -B /tmp/general-authoring-build
cmake --build /tmp/general-authoring-build
ctest --test-dir /tmp/general-authoring-build --output-on-failure
```

An existing Qt 6 installation must provide the Core, Network and Widgets CMake
packages. This focused project does not change or verify the production GUI's
Qt 6.11 minimum; it has been run with the cloud environment's Qt 6.8.2.

The suite includes Lua lexical masking edge cases and exact 64 KiB inputs.
The scanner visits each character a bounded number of times without allocating
a source suffix or call prefix at each position. This is an availability check;
lexical lint still does not establish runtime safety or gameplay correctness.

For an informational timing run (no timing threshold in CTest):

```sh
QT_QPA_PLATFORM=offscreen /tmp/general-authoring-build/general_authoring_tests --benchmark-lexical
```

This measures full validation of ordinary padding, many spaced method calls,
and long-bracket delimiter decoys at 8, 16, 32 and nearly 64 KiB. Fixture creation
is outside the timer. Compare builds on the same machine and configuration;
single debug-build measurements are not a performance guarantee.
