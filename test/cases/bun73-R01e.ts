// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function fail(): never;
declare function c(): boolean;
declare function use(v: unknown): void;
declare const arr: number[];
type U = string | number | boolean | null | undefined;
function f4(x: string | number) { fail(); do { try { } finally { x.foo; } } while (true); }
function f7(x: string | number) { fail(); do { try { } finally { try { x.foo; } finally { } } } while (true); }
function plainLoop(x: string | number) { fail(); do { x.foo; } while (true); }
function plainFinally(x: string | number) { fail(); try { } finally { x.foo; } }
// the other way round: tsc reports TS2339 on `never` at the `o.x` of the switch, bun does not
function f43(o: { x: U }) { fail(); do { try { } catch { try { } catch { use(o.x); } switch (typeof o.x) { } } } while (c() && o.x == null); if (typeof o.x === "boolean") { } }
// `arr`, a global constant, is `never` in bun (TS2488), not in tsc
function f140(o: { n: { x: U; y: U } }) { try { fail(); } finally { switch (typeof o.n.y) { case "boolean": do { } while (typeof o.n.x === "boolean"); for (const e of arr) { } } } for (const e of arr) { } }
const sentinel: number = "";
