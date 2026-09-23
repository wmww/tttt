// @strict: true
// Object literal freshness: excess property checks (TS2353) only for fresh object literals
interface P { id: number; opt?: string }
var foo: { id: number } = { id: 4, name: "foo" };
const p: P = { id: 1, nme: "x" };
const regular = { id: 1, extra: true };
const ok: P = regular;
function take(p: P) {}
take({ id: 1, other: 2 });
const nested: { inner: P } = { inner: { id: 1, deep: 0 } };
type U = { kind: "a"; a: number } | { kind: "b"; b: number };
const u: U = { kind: "a", a: 1, b: 2 };
const asserted = { id: 1, more: 1 } as P;
const idx: { [k: string]: number } = { anything: 1 };
const empty: {} = { x: 1 };
const obj: object = { x: 1 };
function ret(): P { return { id: 1, zzz: 1 }; }
const spread: P = { ...regular, id: 2 };
