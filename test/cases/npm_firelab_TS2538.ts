// origin: npm firelab@0.1.0 https://github.com/AkilSaji/firelab tsconfig.bench.json seed 20261023
// flip: yes
// A truthiness-narrowed generic indexed access (NonNullable<Partial<Record<K, keyof Caps>>[M]>) indexes Caps: no TS2538
// @strict: true
type Caps = { a: boolean };
declare const CAP_FOR: Partial<Record<"x" | "y", keyof Caps>>;
function f<M extends "x" | "y">(caps: Caps, method: M) {
    const cap = CAP_FOR[method];
    if (cap) caps[cap];
}
