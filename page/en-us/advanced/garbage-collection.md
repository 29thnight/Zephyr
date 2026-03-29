# Garbage Collection (Generational)

> [!IMPORTANT] Sustaining optimal frame pacing (60/144hz)
> Common stop-the-world implementations notoriously trigger gameplay hitches inside loops.

Zephyr's architectural foundation utilizes 4-Space Heap tracking (`Nursery`, `Small Old`, `Large Object`, `Pinned`), coupled alongside memory fragmentation compactions resolving execution bottlenecks completely. Incremental pipeline specifications to follow.
