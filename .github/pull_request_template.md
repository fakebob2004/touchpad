## What changed

<!-- Describe the behavior and the reason for the change. -->

## Validation

- [ ] `make test`
- [ ] Windows `ctest`
- [ ] Real-device validation, when input behavior changed
- [ ] English and Chinese README updated, when public behavior changed
- [ ] Third-party notices updated, when external code or assets were added

## Safety checklist

- [ ] Networking remains in user mode
- [ ] Kernel input is bounded and validated
- [ ] Disconnect and timeout release all contacts
- [ ] No credentials, certificates, captures, or generated binaries are committed
