// @strict: true
// @target: es2020
type EnumLike = Readonly<Record<string, string | number>>;
type Out<T extends EnumLike> = T[keyof T] & {};
declare function enc(m: Map<Out<{ a: "a"; b: "b" }>, number>): void;
enc(new Map([["b", 2]]));
declare function enc2(m: Map<"a" | "b", number>): void;
enc2(new Map([["b", 2]]));
type O2 = Out<{ a: "a"; b: "b" }>;
declare const o2: O2;
