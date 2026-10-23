// @strict: true
// @target: es2022
// Spreading objects with index signatures: the index signature's value is the union of theirs by getUnionType's
// literal reduction, not subtypes (tsc's getUnionIndexInfos), so identical function types stay apart (openclaw's
// shiki language table, 29 `() => Promise<typeof import(...)>` that tttt collapsed into one)
const f1 = () => Promise.resolve({ x: 1 });
const f2 = () => Promise.resolve({ x: 1 });
const f3 = () => Promise.resolve({ x: 1 });
const info = [{ id: "a", al: ["q"], f: f1 }, { id: "b", f: f2 }, { id: "c", al: ["r"], f: f3 }] as const;
export const base = Object.fromEntries(info.map((l) => [l.id, l.f]));
export const alias = Object.fromEntries(info.flatMap((l) => ("al" in l ? l.al : []).map((a) => [a, l.f])));
export const all = { ...base, ...alias };
declare const a: { [k: string]: () => { y: string } };
declare const b: { [k: string]: () => { y: string } };
export const both = { ...a, ...b };
