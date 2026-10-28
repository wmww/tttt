// @strict: true
// Known failing (cor, 2026-10-28): tsgo reports TS7022 on `d`, the tuple pattern's element read inside a function of
// its own initializer; tttt reports nothing (agents/bugs/missing_checks.md)
const [d, e]: [number, (n: number) => number] = [1, (n) => e(n) + d];
