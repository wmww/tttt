// @strict: true
// `typeof import("./m").it.runIf`: past the module's exports, a name is a property of the previous value's type by
// tsc's getPropertyOfType, an intersection's too (vitest's `it: TestAPI`, an intersection: openclaw's 56 errors); a
// missing one is TS2694 on the export as found, an `export { it }` specifier in the module
// @filename: m.ts
type Chain<F extends (...args: any) => any> = F & { fn: F };
interface Ext { runIf(c: boolean): (name: string) => void; skipIf: (c: any) => number }
type TestAPI<X = object> = Chain<(name: string) => void> & Ext & { each: { deep: number } };
declare const it: TestAPI;
export { it };
// @filename: a.ts
type A = ReturnType<typeof import("./m").it.runIf>;
type B = typeof import("./m").it.each.deep;
type C = typeof import("./m").it.skipIf;
type D = typeof import("./m").it.missing;
export const a: A = (name: string) => {};
export const b: B = 1;
