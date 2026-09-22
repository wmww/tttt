// @strict: true
declare const a: [] | [signature: string];
a.length;
declare const b: readonly [string] | readonly [string, number];
b.length;
declare const c: [];
c.length;
declare const d: [string, number?];
d.length;
declare const e: readonly [string, ...number[]];
e.length;
e.map(x => x);
