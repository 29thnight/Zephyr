# Host Handle Policy

> [!WARNING] Guarding Native Scopes against Memory Leaks
> Unsafe C++ pointers dangling across script boundaries cause irrecoverable application crashes natively.

Zephyr enforces 4 distinctive lifecycle domains (`Frame`, `Tick`, `Persistent`, `Stable`) upon pointer references mapped from the Host, sealing vulnerability channels implicitly. Specific handler documentation will be appended shortly.
