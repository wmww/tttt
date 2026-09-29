# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-29 (triage session, todo entry 10 of the seventh window; suite 12318 -> 12354, 0 lost, corpora
unchanged). Four Opus sub-agents took every item: fixed ~25 (the harness now drops diagnostics its type walk provokes,
which hid a TS2318 generator check; `declare global` names resolve to the merged global first (Iterator.from); pattern
ambient module augmentations; tuple members with the tuple as `this`; computed ambient enum members; TS2589 at the
declaration; tsc's overload fallback; Pick's K inference; destructuring accessibility; DOS drive paths; file names in
messages; TS2395/TS2307/TS2748/TS1376 leftovers; `@augments` as the base; TS7031 widening...), routed the rest to
`bugs/*.md`, plan 38 (nested_generic) and plan 39 (TS7023 re-entrance). The drizzle "CLI-only" probe was not
reproducible. Stale entries (tests that pass now) were pruned from `bugs/*.md`.

- (triage 2026-09-29, group A) `test/probes/triage/pick_key_inference.ts`: tsc's TS2353 excess-property error on an
  object literal against `Pick<S, keyof S>` is missing (predates the Pick K inference fix).
