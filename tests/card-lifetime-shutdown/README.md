# Card lifetime shutdown fixture

[`shutdown_protocol_test.cpp`](shutdown_protocol_test.cpp) is compiled into
`qsanguosha_runtime_tests` by [CMakeLists.txt](../CMakeLists.txt).
[`main()` in runtime-tests-main.cpp](../runtime-tests-main.cpp) dispatches it as:

```text
qsanguosha_runtime_tests --suite card-lifetime-shutdown [case]
```

The default case exercises worker finalization, owner-thread deletion and
idempotent shutdown. `overlap` checks isolation between rooms;
`lua-exception-unwind` checks that Lua invocation frames and pins are released
before shutdown. The `worker`, `lease`, `reservation`, and `lua-pin` cases
leave an outstanding resource deliberately: each must exit nonzero without a
`CARD_LIFETIME_ZERO` record.

See the [ownership contract](../../docs/card-lifetime-ownership.md) for the
shutdown stages and lifetime rules.
