// @strict: true
// A type parameter name repeated in one list is one type parameter (tsc's binder merges the declarations into one
// symbol; TS2300 aside): `interface Rep<T, T>` takes one type argument (several_types.c declared_type_params(), bun
// 99-V4 getDeclaredTypeOfClassOrInterface__05)
interface Rep<T, T> { value: T; }
class Twice<K, K> { key!: K; }
type Al<U, U> = { u: U };
declare function f<V, V>(v: V): V;
declare const r1: Rep<string>;
declare const r2: Rep<string, number>;
declare const t1: Twice<boolean>;
declare const a1: Al<number>;
declare const a2: Al<number, string>;
const x1: never = r1.value;
const x2: never = t1.key;
const x3: never = a1.u;
const x4: never = f(1);
const x5: never = f<string>("");
const x6: never = f<string, number>("");
