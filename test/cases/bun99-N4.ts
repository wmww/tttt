// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"] } }
// @filename: a.ts
type A = { kind: "a"; x: 1 };
type B = { kind: "b"; y: 2 };
enum E { One, Two }
class K { k = 1; private p = 2; m() {} get g() { return 1; } static s = 1 }
interface I { (): void; prop: 1 }
declare function id<T>(value: T): T;
declare function memo<T>(factory: () => T): T;
declare const b1: NoInfer<A & B>; declare const o1: NoInfer<A & B> | undefined; // never by discriminants
{ const v: never = { id: 1, ...b1 }; }
{ const v: never = { ...b1, id: 1 }; }
{ const v: never = id({ id: 1, ...b1 }); }
{ const v: never = memo(() => ({ id: 1, ...b1 })); }
{ const v: never = id(o1 ? { id: 1, ...o1 } : undefined); }
{ const v: never = id(o1 !== undefined ? { ...o1, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o1 }); }
{ const v: never = id({ ...b1, ...b1 }); }
declare const b2: NoInfer<{}>; declare const o2: NoInfer<{}> | undefined; // empty
{ const v: never = { id: 1, ...b2 }; }
{ const v: never = { ...b2, id: 1 }; }
{ const v: never = id({ id: 1, ...b2 }); }
{ const v: never = memo(() => ({ id: 1, ...b2 })); }
{ const v: never = id(o2 ? { id: 1, ...o2 } : undefined); }
{ const v: never = id(o2 !== undefined ? { ...o2, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o2 }); }
{ const v: never = id({ ...b2, ...b2 }); }
declare const b3: NoInfer<object>; declare const o3: NoInfer<object> | undefined; // object
{ const v: never = { id: 1, ...b3 }; }
{ const v: never = { ...b3, id: 1 }; }
{ const v: never = id({ id: 1, ...b3 }); }
{ const v: never = memo(() => ({ id: 1, ...b3 })); }
{ const v: never = id(o3 ? { id: 1, ...o3 } : undefined); }
{ const v: never = id(o3 !== undefined ? { ...o3, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o3 }); }
{ const v: never = id({ ...b3, ...b3 }); }
declare const b4: NoInfer<unknown>; declare const o4: NoInfer<unknown> | undefined; // unknown
{ const v: never = { id: 1, ...b4 }; }
{ const v: never = { ...b4, id: 1 }; }
{ const v: never = id({ id: 1, ...b4 }); }
{ const v: never = memo(() => ({ id: 1, ...b4 })); }
{ const v: never = id(o4 ? { id: 1, ...o4 } : undefined); }
{ const v: never = id(o4 !== undefined ? { ...o4, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o4 }); }
{ const v: never = id({ ...b4, ...b4 }); }
declare const b5: NoInfer<any>; declare const o5: NoInfer<any> | undefined; // any
{ const v: never = { id: 1, ...b5 }; }
{ const v: never = { ...b5, id: 1 }; }
{ const v: never = id({ id: 1, ...b5 }); }
{ const v: never = memo(() => ({ id: 1, ...b5 })); }
{ const v: never = id(o5 ? { id: 1, ...o5 } : undefined); }
{ const v: never = id(o5 !== undefined ? { ...o5, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o5 }); }
{ const v: never = id({ ...b5, ...b5 }); }
declare const b6: NoInfer<never>; declare const o6: NoInfer<never> | undefined; // never
{ const v: never = { id: 1, ...b6 }; }
{ const v: never = { ...b6, id: 1 }; }
{ const v: never = id({ id: 1, ...b6 }); }
{ const v: never = memo(() => ({ id: 1, ...b6 })); }
{ const v: never = id(o6 ? { id: 1, ...o6 } : undefined); }
{ const v: never = id(o6 !== undefined ? { ...o6, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o6 }); }
{ const v: never = id({ ...b6, ...b6 }); }
declare const b7: NoInfer<null>; declare const o7: NoInfer<null> | undefined; // null
{ const v: never = { id: 1, ...b7 }; }
{ const v: never = { ...b7, id: 1 }; }
{ const v: never = id({ id: 1, ...b7 }); }
{ const v: never = memo(() => ({ id: 1, ...b7 })); }
{ const v: never = id(o7 ? { id: 1, ...o7 } : undefined); }
{ const v: never = id(o7 !== undefined ? { ...o7, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o7 }); }
{ const v: never = id({ ...b7, ...b7 }); }
declare const b8: NoInfer<undefined>; declare const o8: NoInfer<undefined> | undefined; // undefined
{ const v: never = { id: 1, ...b8 }; }
{ const v: never = { ...b8, id: 1 }; }
{ const v: never = id({ id: 1, ...b8 }); }
{ const v: never = memo(() => ({ id: 1, ...b8 })); }
{ const v: never = id(o8 ? { id: 1, ...o8 } : undefined); }
{ const v: never = id(o8 !== undefined ? { ...o8, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o8 }); }
{ const v: never = id({ ...b8, ...b8 }); }
declare const b9: NoInfer<string>; declare const o9: NoInfer<string> | undefined; // string
{ const v: never = { id: 1, ...b9 }; }
{ const v: never = { ...b9, id: 1 }; }
{ const v: never = id({ id: 1, ...b9 }); }
{ const v: never = memo(() => ({ id: 1, ...b9 })); }
{ const v: never = id(o9 ? { id: 1, ...o9 } : undefined); }
{ const v: never = id(o9 !== undefined ? { ...o9, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o9 }); }
{ const v: never = id({ ...b9, ...b9 }); }
declare const b10: NoInfer<number | { a: 1 }>; declare const o10: NoInfer<number | { a: 1 }> | undefined; // number or object
{ const v: never = { id: 1, ...b10 }; }
{ const v: never = { ...b10, id: 1 }; }
{ const v: never = id({ id: 1, ...b10 }); }
{ const v: never = memo(() => ({ id: 1, ...b10 })); }
{ const v: never = id(o10 ? { id: 1, ...o10 } : undefined); }
{ const v: never = id(o10 !== undefined ? { ...o10, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o10 }); }
{ const v: never = id({ ...b10, ...b10 }); }
declare const b11: NoInfer<[1, 2]>; declare const o11: NoInfer<[1, 2]> | undefined; // tuple
{ const v: never = { id: 1, ...b11 }; }
{ const v: never = { ...b11, id: 1 }; }
{ const v: never = id({ id: 1, ...b11 }); }
{ const v: never = memo(() => ({ id: 1, ...b11 })); }
{ const v: never = id(o11 ? { id: 1, ...o11 } : undefined); }
{ const v: never = id(o11 !== undefined ? { ...o11, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o11 }); }
{ const v: never = id({ ...b11, ...b11 }); }
declare const b12: NoInfer<string[]>; declare const o12: NoInfer<string[]> | undefined; // array
{ const v: never = { id: 1, ...b12 }; }
{ const v: never = { ...b12, id: 1 }; }
{ const v: never = id({ id: 1, ...b12 }); }
{ const v: never = memo(() => ({ id: 1, ...b12 })); }
{ const v: never = id(o12 ? { id: 1, ...o12 } : undefined); }
{ const v: never = id(o12 !== undefined ? { ...o12, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o12 }); }
{ const v: never = id({ ...b12, ...b12 }); }
declare const b13: NoInfer<() => void>; declare const o13: NoInfer<() => void> | undefined; // function
{ const v: never = { id: 1, ...b13 }; }
{ const v: never = { ...b13, id: 1 }; }
{ const v: never = id({ id: 1, ...b13 }); }
{ const v: never = memo(() => ({ id: 1, ...b13 })); }
{ const v: never = id(o13 ? { id: 1, ...o13 } : undefined); }
{ const v: never = id(o13 !== undefined ? { ...o13, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o13 }); }
{ const v: never = id({ ...b13, ...b13 }); }
declare const b14: NoInfer<K>; declare const o14: NoInfer<K> | undefined; // class
{ const v: never = { id: 1, ...b14 }; }
{ const v: never = { ...b14, id: 1 }; }
{ const v: never = id({ id: 1, ...b14 }); }
{ const v: never = memo(() => ({ id: 1, ...b14 })); }
{ const v: never = id(o14 ? { id: 1, ...o14 } : undefined); }
{ const v: never = id(o14 !== undefined ? { ...o14, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o14 }); }
{ const v: never = id({ ...b14, ...b14 }); }
declare const b15: NoInfer<I>; declare const o15: NoInfer<I> | undefined; // callable interface
{ const v: never = { id: 1, ...b15 }; }
{ const v: never = { ...b15, id: 1 }; }
{ const v: never = id({ id: 1, ...b15 }); }
{ const v: never = memo(() => ({ id: 1, ...b15 })); }
{ const v: never = id(o15 ? { id: 1, ...o15 } : undefined); }
{ const v: never = id(o15 !== undefined ? { ...o15, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o15 }); }
{ const v: never = id({ ...b15, ...b15 }); }
declare const b16: NoInfer<Record<string, number>>; declare const o16: NoInfer<Record<string, number>> | undefined; // record
{ const v: never = { id: 1, ...b16 }; }
{ const v: never = { ...b16, id: 1 }; }
{ const v: never = id({ id: 1, ...b16 }); }
{ const v: never = memo(() => ({ id: 1, ...b16 })); }
{ const v: never = id(o16 ? { id: 1, ...o16 } : undefined); }
{ const v: never = id(o16 !== undefined ? { ...o16, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o16 }); }
{ const v: never = id({ ...b16, ...b16 }); }
declare const b17: NoInfer<{ [k: string]: number } & { a: 1 }>; declare const o17: NoInfer<{ [k: string]: number } & { a: 1 }> | undefined; // index and property
{ const v: never = { id: 1, ...b17 }; }
{ const v: never = { ...b17, id: 1 }; }
{ const v: never = id({ id: 1, ...b17 }); }
{ const v: never = memo(() => ({ id: 1, ...b17 })); }
{ const v: never = id(o17 ? { id: 1, ...o17 } : undefined); }
{ const v: never = id(o17 !== undefined ? { ...o17, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o17 }); }
{ const v: never = id({ ...b17, ...b17 }); }
declare const b18: NoInfer<E>; declare const o18: NoInfer<E> | undefined; // enum
{ const v: never = { id: 1, ...b18 }; }
{ const v: never = { ...b18, id: 1 }; }
{ const v: never = id({ id: 1, ...b18 }); }
{ const v: never = memo(() => ({ id: 1, ...b18 })); }
{ const v: never = id(o18 ? { id: 1, ...o18 } : undefined); }
{ const v: never = id(o18 !== undefined ? { ...o18, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o18 }); }
{ const v: never = id({ ...b18, ...b18 }); }
declare const b19: NoInfer<symbol>; declare const o19: NoInfer<symbol> | undefined; // symbol
{ const v: never = { id: 1, ...b19 }; }
{ const v: never = { ...b19, id: 1 }; }
{ const v: never = id({ id: 1, ...b19 }); }
{ const v: never = memo(() => ({ id: 1, ...b19 })); }
{ const v: never = id(o19 ? { id: 1, ...o19 } : undefined); }
{ const v: never = id(o19 !== undefined ? { ...o19, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o19 }); }
{ const v: never = id({ ...b19, ...b19 }); }
declare const b20: NoInfer<Partial<A>>; declare const o20: NoInfer<Partial<A>> | undefined; // partial
{ const v: never = { id: 1, ...b20 }; }
{ const v: never = { ...b20, id: 1 }; }
{ const v: never = id({ id: 1, ...b20 }); }
{ const v: never = memo(() => ({ id: 1, ...b20 })); }
{ const v: never = id(o20 ? { id: 1, ...o20 } : undefined); }
{ const v: never = id(o20 !== undefined ? { ...o20, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o20 }); }
{ const v: never = id({ ...b20, ...b20 }); }
declare const b21: NoInfer<Readonly<A>>; declare const o21: NoInfer<Readonly<A>> | undefined; // readonly
{ const v: never = { id: 1, ...b21 }; }
{ const v: never = { ...b21, id: 1 }; }
{ const v: never = id({ id: 1, ...b21 }); }
{ const v: never = memo(() => ({ id: 1, ...b21 })); }
{ const v: never = id(o21 ? { id: 1, ...o21 } : undefined); }
{ const v: never = id(o21 !== undefined ? { ...o21, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o21 }); }
{ const v: never = id({ ...b21, ...b21 }); }
declare const b22: NoInfer<A | B>; declare const o22: NoInfer<A | B> | undefined; // union
{ const v: never = { id: 1, ...b22 }; }
{ const v: never = { ...b22, id: 1 }; }
{ const v: never = id({ id: 1, ...b22 }); }
{ const v: never = memo(() => ({ id: 1, ...b22 })); }
{ const v: never = id(o22 ? { id: 1, ...o22 } : undefined); }
{ const v: never = id(o22 !== undefined ? { ...o22, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o22 }); }
{ const v: never = id({ ...b22, ...b22 }); }
declare const b23: NoInfer<A | (A & B)>; declare const o23: NoInfer<A | (A & B)> | undefined; // union with never
{ const v: never = { id: 1, ...b23 }; }
{ const v: never = { ...b23, id: 1 }; }
{ const v: never = id({ id: 1, ...b23 }); }
{ const v: never = memo(() => ({ id: 1, ...b23 })); }
{ const v: never = id(o23 ? { id: 1, ...o23 } : undefined); }
{ const v: never = id(o23 !== undefined ? { ...o23, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o23 }); }
{ const v: never = id({ ...b23, ...b23 }); }
declare const b24: NoInfer<A & { z: 3 }>; declare const o24: NoInfer<A & { z: 3 }> | undefined; // intersection
{ const v: never = { id: 1, ...b24 }; }
{ const v: never = { ...b24, id: 1 }; }
{ const v: never = id({ id: 1, ...b24 }); }
{ const v: never = memo(() => ({ id: 1, ...b24 })); }
{ const v: never = id(o24 ? { id: 1, ...o24 } : undefined); }
{ const v: never = id(o24 !== undefined ? { ...o24, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o24 }); }
{ const v: never = id({ ...b24, ...b24 }); }
declare const b25: NoInfer<{ a?: 1; b: 2 | undefined }>; declare const o25: NoInfer<{ a?: 1; b: 2 | undefined }> | undefined; // optional members
{ const v: never = { id: 1, ...b25 }; }
{ const v: never = { ...b25, id: 1 }; }
{ const v: never = id({ id: 1, ...b25 }); }
{ const v: never = memo(() => ({ id: 1, ...b25 })); }
{ const v: never = id(o25 ? { id: 1, ...o25 } : undefined); }
{ const v: never = id(o25 !== undefined ? { ...o25, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o25 }); }
{ const v: never = id({ ...b25, ...b25 }); }
declare const b26: NoInfer<NoInfer<A>>; declare const o26: NoInfer<NoInfer<A>> | undefined; // nested NoInfer
{ const v: never = { id: 1, ...b26 }; }
{ const v: never = { ...b26, id: 1 }; }
{ const v: never = id({ id: 1, ...b26 }); }
{ const v: never = memo(() => ({ id: 1, ...b26 })); }
{ const v: never = id(o26 ? { id: 1, ...o26 } : undefined); }
{ const v: never = id(o26 !== undefined ? { ...o26, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o26 }); }
{ const v: never = id({ ...b26, ...b26 }); }
declare const b27: NoInfer<`a${string}`>; declare const o27: NoInfer<`a${string}`> | undefined; // template
{ const v: never = { id: 1, ...b27 }; }
{ const v: never = { ...b27, id: 1 }; }
{ const v: never = id({ id: 1, ...b27 }); }
{ const v: never = memo(() => ({ id: 1, ...b27 })); }
{ const v: never = id(o27 ? { id: 1, ...o27 } : undefined); }
{ const v: never = id(o27 !== undefined ? { ...o27, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o27 }); }
{ const v: never = id({ ...b27, ...b27 }); }
declare const b28: NoInfer<boolean>; declare const o28: NoInfer<boolean> | undefined; // boolean
{ const v: never = { id: 1, ...b28 }; }
{ const v: never = { ...b28, id: 1 }; }
{ const v: never = id({ id: 1, ...b28 }); }
{ const v: never = memo(() => ({ id: 1, ...b28 })); }
{ const v: never = id(o28 ? { id: 1, ...o28 } : undefined); }
{ const v: never = id(o28 !== undefined ? { ...o28, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o28 }); }
{ const v: never = id({ ...b28, ...b28 }); }
declare const b29: NoInfer<bigint>; declare const o29: NoInfer<bigint> | undefined; // bigint
{ const v: never = { id: 1, ...b29 }; }
{ const v: never = { ...b29, id: 1 }; }
{ const v: never = id({ id: 1, ...b29 }); }
{ const v: never = memo(() => ({ id: 1, ...b29 })); }
{ const v: never = id(o29 ? { id: 1, ...o29 } : undefined); }
{ const v: never = id(o29 !== undefined ? { ...o29, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o29 }); }
{ const v: never = id({ ...b29, ...b29 }); }
declare const b30: NoInfer<Date>; declare const o30: NoInfer<Date> | undefined; // date
{ const v: never = { id: 1, ...b30 }; }
{ const v: never = { ...b30, id: 1 }; }
{ const v: never = id({ id: 1, ...b30 }); }
{ const v: never = memo(() => ({ id: 1, ...b30 })); }
{ const v: never = id(o30 ? { id: 1, ...o30 } : undefined); }
{ const v: never = id(o30 !== undefined ? { ...o30, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o30 }); }
{ const v: never = id({ ...b30, ...b30 }); }
declare const b31: NoInfer<Map<string, 1>>; declare const o31: NoInfer<Map<string, 1>> | undefined; // map
{ const v: never = { id: 1, ...b31 }; }
{ const v: never = { ...b31, id: 1 }; }
{ const v: never = id({ id: 1, ...b31 }); }
{ const v: never = memo(() => ({ id: 1, ...b31 })); }
{ const v: never = id(o31 ? { id: 1, ...o31 } : undefined); }
{ const v: never = id(o31 !== undefined ? { ...o31, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o31 }); }
{ const v: never = id({ ...b31, ...b31 }); }
declare const b32: NoInfer<Promise<1>>; declare const o32: NoInfer<Promise<1>> | undefined; // promise
{ const v: never = { id: 1, ...b32 }; }
{ const v: never = { ...b32, id: 1 }; }
{ const v: never = id({ id: 1, ...b32 }); }
{ const v: never = memo(() => ({ id: 1, ...b32 })); }
{ const v: never = id(o32 ? { id: 1, ...o32 } : undefined); }
{ const v: never = id(o32 !== undefined ? { ...o32, id: 1 } : undefined); }
{ const v: never = id({ id: 1, ...o32 }); }
{ const v: never = id({ ...b32, ...b32 }); }
const sentinel: number = "";
export {};
