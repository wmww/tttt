// @strict: true
declare function f<T>(g: () => { a: T, b: (x: T) => void }): T;
const r1 = f(() => ({ a: 1, b: x => x.toFixed() }));
declare function h<T>(g: () => Promise<{ a: T, b: (x: T) => void }>): T;
const r2 = h(async () => ({ a: "s", b: x => x.length }));
declare function k<T>(g: () => () => { a: T, b: (x: T) => void }): T;
const r3 = k(() => () => ({ a: true, b: x => x }));
const r4 = f(() => { if (Math.random()) return { a: 1, b: x => x }; return { a: 2, b: (x: number) => {} }; });
