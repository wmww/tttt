// @strict: true
// tsc's one isEmptyAnonymousObjectType: an instance of a mapped type with no members, Record<never, never>, isn't one
// (ObjectFlags.Mapped isn't Anonymous): typeof narrowing keeps it, union reduction and relations treat it like tsc
type E = Record<never, never>;
declare const a: E | string;
declare const b: {} | E;
export const u1 = [a, b];
declare let x: unknown;
if (typeof x === "object" && x) { const y: E = x; }
function f1(v: E | number) { if (typeof v === "object") { v; } else { v; } }
function f2(v: E) { const o: object = v; return o; }
function f3<T>(v: T & E) { const o: object = v; return o; }
type U = E | { a: 1 };
declare const w: U;
if ("a" in w) { w; } else { w; }
declare const z: E & string;
export const q = z;
const emp: {} = {};
const m1: E = emp;
function g(v: string | E) { if (v instanceof Object) { v; } }
