# Contributing

Thank you for helping improve the MacBook Precision Touchpad Bridge.

## Before opening a change

- Use an issue for protocol changes, driver ABI changes, or security-sensitive features.
- Keep networking, parsing, pairing, and configuration in user mode.
- Keep kernel input bounded and independently validated.
- Preserve complete-frame semantics and fail-safe contact release.
- Do not add GPL-licensed source code to this Apache-2.0 repository.

## Build and test

macOS:

```sh
make clean
make
make test
```

Windows:

```powershell
cmake -S windows -B out\windows -A x64
cmake --build out\windows --config Release
ctest --test-dir out\windows -C Release --output-on-failure
```

Driver changes must also be tested on Windows 11 with VHF enumeration, feature reports, disconnect,
and timeout release behavior. Never test an unsigned kernel driver on a production machine.

## Pull requests

- Keep changes focused and explain the user-visible impact.
- Add tests for protocol parsing, sequence handling, coordinate mapping, or contact lifecycles.
- Update both `README.md` and `README.zh-CN.md` when changing public behavior.
- Document any new third-party code or assets in `THIRD_PARTY_NOTICES.md`.
- By submitting a contribution, you agree that it is licensed under Apache License 2.0.

## Private and third-party APIs

The macOS capture backend relies on an undocumented Apple framework. Isolate inferred ABI changes in
`mac/Probe/MultitouchSupportABI.h`, test multiple macOS versions when possible, and avoid presenting
private behavior as a stable Apple contract.
