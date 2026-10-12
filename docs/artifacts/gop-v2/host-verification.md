# GOP v2 host verification

Date: 2026-10-12

- `make test-host`: PASS 114 cases; full real main.cpp + renamed startup.cpp.
- `g++ -std=c++17 -fshort-wchar -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g tests/gop_host.cpp -o /tmp/uefi-gop-v2-host-asan`: exit 0.
- `/tmp/uefi-gop-v2-host-asan` outside sandbox after approved escalation: exit 0.
  Exact output: `GOP host checks passed: 114 cases (real application and startup contract).`
- ASan/UBSan emitted no memory, undefined behavior, or leak diagnostic on successful final run.
- Earlier sandbox run hit the known LeakSanitizer ptrace fatal restriction; it was rerun with escalation and succeeded.
- Fixture cleans up allocations after injected FreePool rejection separately from counted application free attempts.
- Startup tests verify stub runtime_init/runtime_fini call contract, not real runtime destructors.
- v1 main snapshot run with the same final114 fixture exits nonzero at the new requirement to LocateProtocol before any console output.

## v1 failure output

```text
exit=-6
FAIL [target ownership replacement pointers and full order] line 185: event("locate") < text_event("Global constructor")
```

## Verified source hashes

- `tests/gop_host.cpp`: `cebf7ab3d1d20679ada59f5facb9090f2177f6ec8374edc3eeeb84d8e45d8167`
- `main.cpp`: `cf264092546c22fb7dd01dc89571fb4ade7e946a9afe6c37a93631c01236bab6`
- `uefi.hpp`: `9de6101ef66cfb5c2aaadbe9a27a1c3b8d78cc9e6a95436ef8ad9b83152bef16`
- `startup.cpp`: `08bcc99c02c719f91372b64d400cf7d83b654790dd673dfa4920dc41ed820d48`
