// @strict: true
type Boxed<T> = { [K in keyof T]: { value: T[K] } };
declare function unbox<T>(b: Boxed<T>): T;
const u = unbox({ a: { value: 1 }, b: { value: "s" } });
declare function rec<K extends string, V>(r: Record<K, V>): [K, V];
const r = rec({ x: 1, y: 2 });
type Keys = "a" | "b" | "c";
declare function opts<T extends Record<string, Keys[]>>(o: { [K in keyof T & Keys]: { deps: T[K]; f: (x: Pick<{ a?: 1, b?: 2, c?: 3 }, K | T[K][number]>) => number } }): T;
const o = opts({ a: { deps: ["b"], f: x => x.a ?? x.b ?? 0 } });
