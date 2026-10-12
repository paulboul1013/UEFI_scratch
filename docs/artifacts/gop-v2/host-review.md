# Independent application ownership and status review

Date: 2026-10-12. Reviewer: host_contract subagent. Scope: final `main.cpp`,
`startup.cpp`, and the host fixture contract against the implementation plan.

No unresolved correctness finding in the reviewed implementation.

- `main.cpp:81`: query/current Info pointers are tested for null and sufficient
  size before reading fields. Version, dimensions, and pixel format are checked.
- `main.cpp:186`: enumeration is ascending over the saved initial MaxMode. OUT
  parameters start null/zero for each call; failed OUT pointers are ignored.
- `main.cpp:195`: candidate data is copied into scalar snapshots before FreePool.
  Each successful nonempty query buffer gets exactly one attempt. The application
  stops on a rejected free and preserves that failure if reporting also fails.
- `main.cpp:208`: original 800x600 wins; otherwise the first valid 800x600 wins.
  Incomplete enumeration distinguishes fallback from a complete missing target.
- `main.cpp:298`: original/current comparison prevents clearing a changed console.
  ClearScreen runs before target switching and is followed by another comparison.
- `main.cpp:312`: even a failing switch marks restoration necessary. Actual
  Mode/Info pointers are read after SetMode and checked against the selected
  number, size, dimensions, version, and format. Query pointers are not reused.
- `main.cpp:334`: final text and DRAW_OK are followed by mode checks before BLT
  and keyboard use. Final normal screen contains three CRLF lines and a marker
  with no trailing newline.
- `main.cpp:355`: the shared exit restores after a switch attempt, current drift,
  or invalid current data. No switch occurs if the unchanged original is retained.
- `main.cpp:362`: restore-announcement failure still permits restoration. A main
  failure takes precedence over restoration/reporting failures; otherwise the
  restore status or validation error becomes final. Restoration is tried once.
- `main.cpp:376`: successful restoration performs no subsequent program output.
- `main.cpp:387`: original scalar state is captured before console output. Every
  return with a reliable original reaches the shared restore helper.
- `startup.cpp`: runtime_init, real efi_main, runtime_fini occur in that order and
  the application status is preserved. Host stubs verify this call contract only.

Evidence: final `make test-host` passes 114 cases; full stdout and exit status
are at `/tmp/uefi-gop-v2-host-final.log`. The ASan/UBSan/LeakSanitizer build with
`-Werror` passes the same 114 cases outside the sandbox after approved escalation.
Commands, exact result, hashes, v1 red output, and limits are recorded in
`/tmp/uefi-gop-v2-host-verification.md`. Independent peers also compiled and ran
the earlier 102/113-case stages and reviewed the fixture ownership model.

The fixture uses actual malloc sizes for short and extended query buffers, a
poisoned failed OUT pointer, real frees, and freshly allocated replacement
Mode/Info objects with old allocations destroyed. It verifies exact ownership,
free attempt count, FreePool-before-text, switch/clear/BLT/key/restore order,
distinct primary/restore statuses, and restore-before-fini on representative
successful and failed paths. Injected rejected frees are fixture-cleaned without
counting that as successful product cleanup.
