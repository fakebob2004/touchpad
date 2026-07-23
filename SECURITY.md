# Security policy

## Supported status

This repository is an experimental prototype. It currently has no stable release or production
support window.

The receiver accepts input from a trusted LAN over unauthenticated, unencrypted TCP. Restrict port
39871 to a Private network and LocalSubnet. Do not expose it to the internet or an untrusted LAN.
The Windows driver currently uses test signing and should run only on a dedicated development
machine.

## Reporting a vulnerability

Prefer GitHub's private vulnerability reporting for this repository. If that option is unavailable,
open an issue containing only a high-level description and request a private contact channel. Do not
publish exploit details, credentials, private keys, driver certificates, or sensitive captures in a
public issue.

Useful reports include:

- network input that bypasses MTP1 validation;
- a path to unbounded kernel input or memory corruption;
- stuck contacts after disconnect, timeout, or malformed input;
- unsafe driver installation or privilege-boundary behavior;
- accidental inclusion of signing material or secrets.
