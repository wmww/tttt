// @noTypesAndSymbols: true
// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: limit_01.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f());
// @filename: limit_02.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(undefined));
// @filename: limit_03.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const x: Json = f();
// @filename: limit_04.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: number, opts?: Check<T>): T;
take(f(1));
// @filename: limit_05.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: Check<T>, opts?: number): T;
take(f(undefined, 1));
// @filename: limit_06.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare class C<T = unknown> { constructor(opts?: Check<T>); value: T }
const c: C<Json> = new C();
// @filename: limit_07.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
f<Json>();
// @filename: limit_08.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const g = f<Json>;
// @filename: limit_09.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const g = f<Json>; g();
// @filename: limit_10.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const g: (opts?: 1) => Json = f;
// @filename: limit_11.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(...opts: Check<T>[]): T;
take(f());
// @filename: limit_12.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(strings: TemplateStringsArray, opts?: Check<T>): T;
take(f``);
// @filename: limit_13.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
take(i.m());
// @filename: limit_14.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
take(i.m(1));
// @filename: limit_15.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(), 1);
// @filename: limit_16.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const n: number = f<Json>();
// @filename: limit_17.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts: Check<T>): T;
take(f());
// @filename: limit_18.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a: number, opts?: Check<T>): T;
take(f("x"));
// @filename: limit_19.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function f<T = unknown>(a: number, opts?: Check<T>): T;
take(f("x"));
// @filename: limit_20.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
function h() { return f<Json>(); }
take(h());
// @filename: limit_21.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f());
type A = Check<Json>;
// @filename: limit_22.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
type A = Check<Json>;
take(f());
// @filename: limit_23.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f());
take(f(undefined));
// @filename: limit_24.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(undefined));
take(f());
take(f(undefined));
// @filename: limit_25.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts: Check<T>): T }
declare const i: I<Json>;
take(i.m());
// @filename: limit_26.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a: number, b: Check<T>): T;
take(f("x", 1));
// @filename: limit_27.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(a: number, opts: Check<T>): T }
declare const i: I<Json>;
take(i.m("x", 1));
// @filename: limit_28.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(a: Check<T>, b: number): T }
declare const i: I<Json>;
take(i.m());
// @filename: limit_29.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m<U>(a: U, opts?: Check<T>): T }
declare const i: I<Json>;
take(i.m(1));
// @filename: limit_30.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m<U>(a: U, opts?: Check<T>): T }
declare const i: I<Json>;
take(i.m(1, undefined));
// @filename: limit_31.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(cb: (x: T) => void, opts?: Check<T>): T;
take(f(x => { x; }));
// @filename: limit_32.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: { cb: (x: T) => void }, opts?: Check<T>): T;
take(f({ cb(x) { x; } }));
// @filename: limit_33.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function take2(a: Json, b: Json): void;
take2(f(), f(undefined));
// @filename: limit_34.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
function h() { take(f()); take(f(undefined)); return f<Json>(); }
const r = h();
// @filename: limit_35.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const o = { a: f<Json>(), b: 1 };
const n: string = o.b;
// @filename: limit_36.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): Promise<T>;
async function g(): Promise<Json> { return await f(); }
// @filename: limit_37.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function dec<T>(opts?: Check<T>): (target: any) => void;
@dec<Json>() class K {}
// @filename: limit_38.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
class B<T> { constructor(opts?: Check<T>) {} }
class D extends B<Json> {}
new D();
// @filename: limit_39.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
class B<T> { constructor(opts?: Check<T>) {} }
class D extends B<Json> { constructor() { super(); } }
// @filename: limit_40.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const g: () => Json = f;
// @filename: limit_41.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function call<R>(fn: () => R): R;
take(call(f<Json>));
// @filename: limit_42.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
const g: () => Json = i.m;
// @filename: limit_43.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
interface J { m(): Json }
declare const i: I<Json>;
const j: J = i;
// @filename: limit_44.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
type P = Parameters<typeof i.m>;
// @filename: limit_45.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
type R = ReturnType<typeof i.m>;
// @filename: limit_46.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): asserts this is I<string> }
declare const i: I<Json>;
i.m();
// @filename: limit_47.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(x: unknown, opts?: Check<T>): x is string }
declare const i: I<Json>;
declare const u: unknown;
if (i.m(u)) { u.length; }
// @filename: limit_48.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json> | undefined;
take(i?.m()!);
// @filename: limit_49.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
interface I2<T> { m(opts?: Check<T>): T }
declare const i: I<Json> | I2<Json>;
take(i.m());
// @filename: limit_50.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
const n: number = i.m;
// @filename: limit_51.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(...([] as [])));
// @filename: limit_52.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: number, opts?: Check<T>): T;
declare const xs: [number];
take(f(...xs));
// @filename: limit_53.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: number, opts?: Check<T>): T;
declare const xs: number[];
take(f(...xs));
// @filename: limit_54.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface Th<T> { then(ok?: (v: T) => void, bad?: Check<T>): void }
declare const t: Th<Json>;
async function g() { const v = await t; take(v); }
// @filename: limit_55.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface It<T> { [Symbol.iterator](opts?: Check<T>): Iterator<T> }
declare const t: It<Json>;
for (const v of t) take(v);
// @filename: limit_56.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
interface It<T> { next(a?: undefined, opts?: Check<T>): IteratorResult<T>; [Symbol.iterator](): It<T> }
declare const t: It<Json>;
for (const v of t) take(v);
// @filename: limit_57.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a: string, opts?: Check<T>): T;
declare function f<T = unknown>(a: number, opts?: Check<T>): T;
take(f(1));
// @filename: limit_58.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a: string, opts?: Check<T>): T;
declare function f<T = unknown>(a: number, opts?: Check<T>): T;
take(f(true));
// @filename: limit_59.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const x = f() satisfies Json;
// @filename: limit_60.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const x = f() as Json;
// @filename: limit_61.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
function g(): Json { return f(); }
// @filename: limit_62.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const g = (): Json => f();
// @filename: limit_63.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const o: { a: Json } = { a: f() };
// @filename: limit_64.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
const o: Json[] = [f(), f()];
// @filename: limit_65.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
let x: Json; x = f();
// @filename: limit_66.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare const c: boolean;
take(c ? f() : "a");
// @filename: limit_67.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f() ?? "a");
// @filename: limit_68.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function id<U>(u: U): U;
take(id(f()));
// @filename: limit_69.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): () => T;
take(f()());
// @filename: limit_70.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
class K { p: Json = f(); static s: Json = f(); m(a: Json = f()) {} }
// @filename: limit_71.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(this: Check<T> | void, opts?: number): T;
take(f());
// @filename: limit_72.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare const o: { f<T = unknown>(opts?: Check<T>): T };
take(o.f());
take(o["f"]());
// @filename: limit_73.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a?: Check<T>, b?: Check<T[]>, c?: number): T;
take(f());
// @filename: limit_74.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts: Check<T> | void): T;
take(f());
// @filename: limit_75.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(a: number, opts: Check<T> | void): T;
take(f(1));
// @filename: limit_76.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>({ a }?: { a: Check<T> }): T;
take(f());
// @filename: limit_77.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
function f<T = unknown>(opts: Check<T> | undefined = undefined): T { return null!; }
take(f());
// @filename: limit_78.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function wrap<A extends unknown[], R>(fn: (...a: A) => R): (...a: A) => R;
const g = wrap(f<Json>);
// @filename: limit_79.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
type F = typeof f<Json>;
declare const g: F;
take(g());
// @filename: limit_80.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
f<Json>().length;
f<Json>;
// @filename: argument_02.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f({ a: 1 }));
// @filename: argument_03.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f([]));
// @filename: argument_04.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(() => 1));
// @filename: argument_05.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f(1));
// @filename: argument_06.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
take(f("a"));
// @filename: argument_07.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
declare const o: {};
take(f(o));
// @filename: argument_08.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
interface I<T> { m(opts?: Check<T>): T }
declare const i: I<Json>;
take(i.m({}));
// @filename: argument_09.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(opts?: Check<T>): T;
f<Json>({});
// @filename: argument_11.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare function f<T = unknown>(a?: {}, opts?: Check<T>): T;
take(f({}));
// @filename: plain_01.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b?: Opt<T>, c?: Box<T>): [T];
const r1: never = f(1); const r2: never = f("a", { s: "a" }); const r3: never = f(1, { s: 1 }); const r4: never = f(1, undefined, { v: "x" }); f(); f(1, 2, 3, 4);
// @filename: plain_02.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
interface I<T> { m(a: T, b?: Opt<T>, c?: Box<T>): [T]; n<U>(a: U, b?: Opt<T>, c?: Box<U>): [T, U] }
declare const i: I<string>;
const r1: never = i.m("a"); const r2: never = i.m("a", { n: 1 }); const r3: never = i.n(1); const r4: never = i.n(1, { s: "" }, { v: "" }); i.m(); i.n(); i.m("a", { s: "a" }, { v: "a" }, 1);
// @filename: plain_03.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b: Opt<T> | void, c: Box<T> | void): [T];
const r1: never = f(1); const r2: never = f(1, { n: 1 }); const r3: never = f(1, { s: 1 }); f(); const r4: never = f(1, undefined, { v: "" });
// @filename: plain_04.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
interface I<T> { m(a: T, b: Opt<T> | void, c: Box<T> | void): [T] }
declare const i: I<number>;
const r1: never = i.m(1); i.m(); const r2: never = i.m(1, { s: 1 }); const r3: never = i.m(1, { n: 1 }, { v: "" });
// @filename: plain_05.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(cb: (x: T) => void, init?: T, o?: Opt<T>): T;
const r1: never = f((x: number) => {}); const r2: never = f(x => { const n: never = x; }, 1); const r3: never = f(x => { const n: never = x; }, "a", { s: "b" });
// @filename: plain_06.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a?: (x: T) => void, b?: (x: Box<T>) => void): T;
const r1: never = f(); const r2: never = f(x => { const n: never = x; }); const r3: never = f((x: number) => {}, y => { const n: never = y; });
// @filename: plain_07.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b?: Opt<T>): 1;
declare function f<T>(a: T, b: number, c?: Box<T>): 2;
const r1: never = f(1); const r2: never = f(1, 2); const r3: never = f(1, 2, { v: 1 }); const r4: never = f("a", { s: "a" }); const r5: never = f(1, "x");
// @filename: plain_08.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
class C<T> { constructor(a: T, b?: Opt<T>, c?: Box<T>) {} }
const r1: never = new C(1); const r2: never = new C("a", { s: "a" }); const r3: never = new C(1, { s: 1 }); new C();
class D extends C<string> {} const r4: never = new D("a"); new D(1); new D("a", { n: 1 });
class E<U> extends C<U[]> { constructor(u: U) { super([u]); } }
// @filename: plain_09.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, ...rest: Opt<T>[]): [T];
const r1: never = f(1); const r2: never = f(1, { n: 1 }, { n: 2 }); const r3: never = f("a", { n: 1 });
// @filename: plain_10.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T extends unknown[]>(a?: Box<T>, ...rest: T): T;
const r1: never = f(); const r2: never = f(undefined, 1, "a"); const r3: never = f({ v: [1] }, 1);
// @filename: plain_11.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function pipe<A, B>(a: A, f?: (a: A) => B, g?: (b: B) => void): B;
const r1: never = pipe(1); const r2: never = pipe(1, x => String(x)); const r3: never = pipe(1, x => [x], y => { const n: never = y; });
// @filename: plain_12.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
const p = Promise.resolve(1);
const r1: never = p.then(); const r2: never = p.then(x => String(x)); const r3: never = p.then(undefined, e => 1); const r4: never = p.then(x => x, e => "a"); const r5: never = p.catch(); const r6: never = p.finally();
// @filename: plain_13.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
const a = [1, 2, 3];
const r1: never = a.reduce((x, y) => x + y); const r2: never = a.reduce((x, y) => x + y, ""); const r3: never = a.map(x => String(x)); const r4: never = a.flatMap(x => [x]); const r5: never = a.find(x => x > 1); const r6: never = new Map(); const r7: never = new Set(); const r8: never = new Map([[1, "a"]]); const r9: never = Array.from(a); const r10: never = Array.from(a, x => String(x)); const r11: never = a.slice(); const r12: never = a.concat(); const r13: never = a.splice(0); const r14: never = new WeakMap(); const r15: never = a.fill(1); const r16: never = a.indexOf(1);
// @filename: plain_14.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(this: Box<T>, a?: Opt<T>): T;
declare const o: { v: number; f: typeof f };
const r1: never = o.f(); const r2: never = o.f({ n: 1 }); const r3: never = o.f({ s: 1 }); f();
// @filename: plain_15.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function tag<T>(s: TemplateStringsArray, a?: T, b?: Opt<T>): [T];
const r1: never = tag``; const r2: never = tag`${1}`; const r3: never = tag`${1}${{ n: 1 }}`; const r4: never = tag`${1}${{ s: 1 }}`; tag`${1}${2}${3}`;
// @filename: plain_16.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
interface I<T> { m(a?: T, b?: Opt<T>): T }
declare const u: I<string> | I<number>;
const r1: never = u.m(); u.m(1); u.m("a", { s: "a" });
declare const x: I<string> & I<number>;
const r2: never = x.m(); const r3: never = x.m(1); const r4: never = x.m("a", { s: "a" }); x.m(true);
// @filename: plain_17.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T = string>(a?: Opt<T>, b?: Box<T>): T;
const r1: never = f(); const r2: string = f(); const r3: number = f(); const r4: number = f({ n: 1 }); const r5: number = f({ s: "" }); const r6: never = f(undefined, { v: 1 });
// @filename: plain_18.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function dec<T>(a?: Opt<T>): (target: new () => T) => void;
@dec() class K1 {} @dec<string>({ s: "" }) class K2 {} @dec<K3>({ n: null! }) class K3 {}
// @filename: plain_19.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
function f<T>(a: T, b: Opt<T> = null!, { v }: Box<T> = null!): [T] { return [a]; }
const r1: never = f(1); const r2: never = f(1, { n: 1 }); const r3: never = f(1, { s: 1 }); const r4: never = f(1, undefined, { v: "" });
// @filename: plain_20.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b?: Opt<T>): [T];
declare function g<A extends unknown[], R>(fn: (...a: A) => R): [A, R];
const r1: never = g(f); const r2: never = g(f<string>); const h: (a: number) => [number] = f; const k: (a: number, b: { s: number }) => [number] = f;
type P = Parameters<typeof f<number>>; const r3: never = null! as P;
// @filename: plain_21.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
interface I<T> { m(a: T, b?: Opt<T>): asserts a is T & { x: 1 }; p(a: unknown, b?: Opt<T>): a is T }
declare const i: I<string>; declare const s: string; declare const u: unknown;
i.m(s); const r1: never = s; if (i.p(u)) { const r2: never = u; }
// @filename: plain_22.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
interface Th<T> { then(ok?: (v: T) => void, bad?: Opt<T>): void }
declare const t: Th<string>;
async function g() { const r1: never = await t; }
interface It<T> { [Symbol.iterator](o?: Opt<T>): { next(a?: undefined, o?: Opt<T>): IteratorResult<T, Box<T>> } }
declare const it: It<number>;
for (const v of it) { const r2: never = v; } const [x] = it; const r3: never = x; const r4: never = [...it];
function* gen() { const r5: never = yield* it; }
// @filename: plain_23.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b?: Opt<T>): [T];
declare const xs: [number]; declare const ys: [number, { n: number }]; declare const zs: number[]; declare const ws: [string, { n: 1 }];
const r1: never = f(...xs); const r2: never = f(...ys); const r3: never = f(...zs); const r4: never = f(...ws);
// @filename: plain_24.ts
export {}
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function f<T>(a: T, b?: Opt<T>): [T];
const r1: never = f?.(1); declare const o: { f?: typeof f } | undefined; const r2: never = o?.f?.(1); const r3: never = o?.f?.("a", { n: 1 });
// @filename: jsx_01.tsx
export {}
declare global { namespace JSX { interface Element {} interface IntrinsicElements { div: { data?: Json } } } }
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? 1 : 2;
declare function f<T = unknown>(opts?: Check<T>): T;
declare function C(props: { data: Json }): JSX.Element;
declare function G<T>(props: { data: T }, context?: Check<T>): JSX.Element;
const a = <div data={f()} />;
const b = <C data={f()} />;
const c = <C {...{ data: f<Json>() }} />;
const d = <G data={f<Json>()} />;
const e = <C data={f(undefined)} />;
// @filename: overload_01.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
declare function f<T = unknown>(a: string, opts: Check<T>): T;
declare function f<T = unknown>(a: number, opts: { a?: 1 }): T;
take(f(n, o));
// @filename: overload_02.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts: Check<T>): T; m(a: number, opts: { a?: 1 }): T }
declare const i: I<Json>;
take(i.m(n, o));
// @filename: overload_03.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts?: Check<T>): T; m(a: number, opts?: { a?: 1 }): T }
declare const i: I<Json>;
take(i.m(n, o));
// @filename: overload_04.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts?: Check<T>): T; m(a: number, opts?: { a?: 1 }): T }
declare const i: I<Json>;
take(i.m(n, { a: 1 }));
// @filename: overload_05.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts?: Check<T>): T; m(a: number, opts?: { a?: 1 }): T }
declare const i: I<Json>;
take(i.m(n));
// @filename: overload_06.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts: Check<T>, z: number): T; m(a: number, opts: { a?: 1 }, z: number): T }
declare const i: I<Json>;
take(i.m(n, o, n));
// @filename: overload_07.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(a: string, opts: Check<T>, z: number | void): T; m(a: number, opts: { a?: 1 }, z: number): T }
declare const i: I<Json>;
take(i.m(n, o, n));
// @filename: overload_08.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(opts: Check<T>, z: number): T }
declare const i: I<Json>;
take(i.m());
// @filename: overload_09.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(y: number, opts: Check<T>, z: number): T }
declare const i: I<Json>;
take(i.m());
// @filename: overload_10.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(y: number | void, opts: Check<T>, z: number): T }
declare const i: I<Json>;
take(i.m());
// @filename: overload_11.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
interface I<T> { m(y: number, opts: Check<T>, z: number): T }
declare const i: I<Json>;
take(i.m(n, o, n, n));
// @filename: overload_12.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
declare function take(data: Json): void;
declare const n: number; declare const o: { a?: 1 };
declare class C<T> { constructor(a: string, opts: Check<T>, z: number); constructor(a: number, opts: { a?: 1 }, z: number); v: T }
take(new C<Json>(n, o, n).v);
// @filename: generated_001.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: Check<T[]>, p2?: Box<T>): T;
const y: never = f<Json>((x: Json) => {}, undefined, { a: 1 });
// @filename: generated_002.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T>, p1?: Check<T> | void, p2?: T, p3?: (x: T) => void): T;
declare function f<T = unknown>(p0: Check<T[]>, p1?: (x: T) => void, p2?: T, p3?: T | void): T;
take(f({ v: "a" }, undefined, j, 1, 1));
// @filename: generated_003.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<Check<T>>): T;
declare function f<T = unknown>(p0: Opt<T>, p1: Box<T> | void, p2: Check<T> | void, p3: Check<T> | void): T;
take(f(x => { x; }, j, j, null!));
// @filename: generated_004.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: T | void, p1?: Opt<T>, p2?: [T, Check<T>], p3?: (x: T) => void): T;
const y: never = f(undefined, (x: Json) => {}, j);
// @filename: generated_005.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: T | void, p1?: Opt<T>, p2?: [T, Check<T>], p3?: (x: T) => void): T;
f<Json>({ v: "a" }, 1, j);
// @filename: generated_006.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T> | void, p1?: [T, Check<T>]): T }
declare const i: I<Json>;
const x: Json = i.m();
// @filename: generated_007.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T> | void, p1?: [T, Check<T>]): T }
declare const i: I<Json>;
i.m(1, [], { s: "a" });
// @filename: generated_008.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Box<Check<T>>, p1?: Box<Check<T>>, ...r: Box<T>[]): T;
const x: Json = f();
// @filename: generated_009.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: [T, Check<T>], p1?: Box<T> | void): T }
declare const i: I<Json>;
take(i.m({ v: "a" }, { a: 1 }, "a"));
// @filename: generated_010.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: number, p1?: [T, Check<T>]): T;
const y: never = f<Json>({ v: "a" });
// @filename: generated_011.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: T, p1: Box<Check<T>>, p2?: number); value: T }
take(new C<Json>({ a: 1 }, (x: Json) => {}, 1, { v: "a" }).value);
// @filename: generated_012.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T[]>): T }
declare const i: I<Json>;
i.m({ v: "a" }, x => { x; });
// @filename: generated_013.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T[]>): T }
declare const i: I<Json>;
const y: never = i.m("a", []);
// @filename: generated_014.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T[]>): T }
declare const i: I<Json>;
i.m({ v: "a" }, null!);
// @filename: generated_015.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: (x: T) => void, p1?: Box<T> | void, p2?: Check<T>); value: T }
take(new C<Json>((x: Json) => {}, undefined, { v: 1 }, j).value);
// @filename: generated_016.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1: number, p2?: Check<T[]>): T;
f<Json>(1, [], "a", { s: "a" });
// @filename: generated_017.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: (x: T) => void, p1: Check<T>, p2: T); value: T }
takeN(new C<Json>(x => { x; }, j).value);
// @filename: generated_018.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Opt<T>, ...r: Box<T>[]): T;
declare function f<T = unknown>(p0?: number, p1?: number, p2?: Box<Check<T>>, p3?: T | void): T;
const x: Json = f(null!, j);
// @filename: generated_019.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1: Check<T[]>, p2: Check<T> | void): T;
const x: Json = f<Json>({ v: 1 }, { s: "a" }, null!);
// @filename: generated_020.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T>, p1?: T, p2?: T | void, p3?: Box<T>): T }
declare const i: I<Json>;
takeN(i.m([], null!, 1, null!, { s: "a" }));
// @filename: generated_021.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T>, p1?: T, p2?: T | void, p3?: Box<T>): T }
declare const i: I<Json>;
take(i.m([], j, { v: "a" }, null!, { v: "a" }));
// @filename: generated_022.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: (x: T) => void, p1?: Box<T> | void, p2?: [T, Check<T>], p3?: Box<Check<T>>): T;
declare function f<T = unknown>(p0: Box<T> | void, p1: Check<T> | void, p2: T | void, p3: T | void): T;
const y: never = f({ a: 1 }, "a", { v: "a" }, j);
// @filename: generated_023.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: number, p1?: Check<T[]>): T;
declare function f<T = unknown>(): T;
const x: Json = f();
// @filename: generated_024.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: Check<T[]>, p1?: Opt<T>); value: T }
take(new C<Json>().value);
// @filename: generated_025.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: number, p2?: Check<T> | void, p3?: Opt<T>): T;
const x: Json = f({ v: 1 }, { s: "a" });
// @filename: generated_026.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: number, p2?: Check<T> | void, p3?: Opt<T>): T;
const x: Json = f({ a: 1 }, undefined, x => { x; }, x => { x; });
// @filename: generated_027.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: number, p2?: Check<T> | void, p3?: Opt<T>): T;
take(f<Json>(j, 1, "a", { v: 1 }));
// @filename: generated_028.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0?: (x: T) => void, p1?: Box<Check<T>>): T }
declare const i: I<Json>;
const x: Json = i.m("a");
// @filename: generated_029.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0?: (x: T) => void, p1?: Box<Check<T>>): T }
declare const i: I<Json>;
const x: Json = i.m([]);
// @filename: generated_030.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0?: (x: T) => void, p1?: Box<Check<T>>): T }
declare const i: I<Json>;
takeN(i.m({ s: "a" }));
// @filename: generated_031.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: (x: T) => void, p1: [T, Check<T>]): T;
takeN(f({ v: "a" }, j));
// @filename: generated_032.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: T | void, p1?: [T, Check<T>], p2?: (x: T) => void, p3?: [T, Check<T>]): T;
const x: Json = f(x => { x; }, "a", [], { a: 1 });
// @filename: generated_033.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: T | void, p2?: Check<T[]>, p3?: (x: T) => void): T;
takeN(f<Json>((x: Json) => {}, 1, null!));
// @filename: generated_034.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T>, p1: (x: T) => void, p2: Opt<T>, p3?: Check<T>): T }
declare const i: I<Json>;
take(i.m({ s: "a" }, "a"));
// @filename: generated_035.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T>, p1: (x: T) => void, p2: Opt<T>, p3?: Check<T>): T }
declare const i: I<Json>;
i.m({ v: "a" }, { v: 1 }, { v: 1 }, undefined, null!);
// @filename: generated_036.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T> | void): T;
take(f());
// @filename: generated_037.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T> | void): T;
f<Json>();
// @filename: generated_038.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T, p1?: Check<T> | void, p2?: [T, Check<T>]): T }
declare const i: I<Json>;
const x: Json = i.m({ v: 1 }, 1, j);
// @filename: generated_039.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T, p1?: Check<T> | void, p2?: [T, Check<T>]): T }
declare const i: I<Json>;
const y: never = i.m(x => { x; }, 1, undefined);
// @filename: generated_040.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: Opt<T>, p1: Opt<T>, p2?: Check<T[]>, ...r: T[]); value: T }
new C<Json>({ a: 1 }, x => { x; }, undefined, { s: "a" }).value;
// @filename: generated_041.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: number, p1?: Box<Check<T>>, p2?: (x: T) => void): T;
take(f((x: Json) => {}, (x: Json) => {}, null!));
// @filename: generated_042.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: number, p1?: Box<Check<T>>, p2?: (x: T) => void): T;
take(f<Json>({ v: "a" }, { s: "a" }, null!));
// @filename: generated_043.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1?: Box<T> | void, p2?: Check<T>, p3?: Check<T> | void): T;
f<Json>(x => { x; }, 1, [], x => { x; });
// @filename: generated_044.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Opt<T>, p1?: [T, Check<T>], p2?: Check<T> | void, p3?: T | void, ...r: Box<T>[]): T;
const x: Json = f<Json>(j, { a: 1 }, "a", { a: 1 });
// @filename: generated_045.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: Box<T>, p1?: (x: T) => void, p2?: Check<T> | void, p3?: Opt<T>); value: T }
new C<Json>(null!, { a: 1 }, [], (x: Json) => {}, (x: Json) => {}).value;
// @filename: generated_046.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: T | void, p1?: T, p2?: Check<T>): T }
declare const i: I<Json>;
takeN(i.m(null!, 1, 1));
// @filename: generated_047.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: T | void, p1?: T, p2?: Check<T>): T }
declare const i: I<Json>;
take(i.m(1, { v: "a" }, "a"));
// @filename: generated_048.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: number, p1: Check<T> | void); value: T }
take(new C<Json>(undefined).value);
// @filename: generated_049.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: number, ...r: T[]): T;
declare function f<T = unknown>(p0?: Box<T> | void, p1?: Check<T>): T;
take(f({ a: 1 }));
// @filename: generated_050.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: [T, Check<T>]): T;
declare function f<T = unknown>(p0: (x: T) => void): T;
take(f());
// @filename: generated_051.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1?: Check<T>): T;
declare function f<T = unknown>(p0?: Box<T>, p1?: T | void, p2?: Check<T>, p3?: (x: T) => void): T;
takeN(f({ a: 1 }, j, null!, { a: 1 }));
// @filename: generated_052.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: T, p1?: number, p2?: Box<Check<T>>): T;
const x: Json = f<Json>(j, { a: 1 }, "a");
// @filename: generated_053.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0?: Check<T[]>): T }
declare const i: I<Json>;
take(i.m());
// @filename: generated_054.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T[]>, ...r: [a?: T, b?: Check<T>]): T;
declare function f<T = unknown>(p0?: Check<T[]>, p1?: T | void): T;
const x: Json = f();
// @filename: generated_055.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T[]>, ...r: [a?: T, b?: Check<T>]): T;
declare function f<T = unknown>(p0?: Check<T[]>, p1?: T | void): T;
take(f());
// @filename: generated_056.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1: Box<Check<T>>, p2?: Opt<T>): T;
const y: never = f<Json>({ a: 1 }, j);
// @filename: generated_057.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: number, p1?: Check<T>, p2?: Check<T[]>); value: T }
const y: never = new C<Json>(x => { x; }, j, "a").value;
// @filename: generated_058.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: number, p1?: Box<T> | void, p2?: [T, Check<T>]): T }
declare const i: I<Json>;
const x: Json = i.m({ v: "a" }, { s: "a" }, null!, undefined);
// @filename: generated_059.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: number, p1?: Box<T> | void, p2?: [T, Check<T>]): T }
declare const i: I<Json>;
take(i.m({ a: 1 }, (x: Json) => {}, undefined));
// @filename: generated_060.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: [T, Check<T>], p1?: T, p2?: [T, Check<T>]): T;
const x: Json = f(j, (x: Json) => {}, { v: 1 }, j);
// @filename: generated_061.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1: Box<Check<T>>, p2?: Box<T>, p3?: Box<Check<T>>, ...r: T[]): T;
const y: never = f<Json>(1, j, { v: 1 }, undefined);
// @filename: generated_062.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0: Opt<T>, p1?: Check<T> | void, p2?: Box<U>): T }
declare const i: I<Json>;
const x: Json = i.m(x => { x; });
// @filename: generated_063.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T>, p1: T | void, p2: T): T }
declare const i: I<Json>;
takeN(i.m({ s: "a" }, { v: 1 }));
// @filename: generated_064.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T>, p1: T | void, p2: T): T }
declare const i: I<Json>;
i.m({ v: "a" }, { a: 1 });
// @filename: generated_065.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T>, p1: T | void, p2: T): T }
declare const i: I<Json>;
const y: never = i.m({ v: 1 }, (x: Json) => {});
// @filename: generated_066.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: (x: T) => void, p1?: Check<T> | void, p2?: Check<T>); value: T }
const y: never = new C<Json>("a", "a").value;
// @filename: generated_067.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: Box<T>, p1?: Opt<T>, p2?: Check<T[]>); value: T }
const y: never = new C<Json>({ v: 1 }, { a: 1 }, j).value;
// @filename: generated_068.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(): T;
declare function f<T = unknown>(p0: Opt<T>, p1?: number, p2?: Box<Check<T>>, p3?: (x: T) => void): T;
const x: Json = f(1, { a: 1 }, undefined);
// @filename: generated_069.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(): T;
declare function f<T = unknown>(p0: Opt<T>, p1?: number, p2?: Box<Check<T>>, p3?: (x: T) => void): T;
const x: Json = f({ a: 1 }, "a", "a", { v: "a" });
// @filename: generated_070.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1?: Box<Check<T>>): T;
const x: Json = f<Json>(x => { x; });
// @filename: generated_071.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: number, p1: Box<T>, p2: Check<T>): T;
const x: Json = f(x => { x; }, null!);
// @filename: generated_072.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: number, p1: Box<T>, p2: Check<T>): T;
f<Json>(1, "a", x => { x; });
// @filename: generated_073.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: (x: T) => void, p1?: Box<Check<T>>, p2?: Check<T> | void, p3?: [T, Check<T>]); value: T }
takeN(new C<Json>({ v: 1 }, 1, { v: "a" }, j, []).value);
// @filename: generated_074.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: [T, Check<T>], p1?: Box<T>, p2?: Box<T> | void, p3?: Opt<T>): T }
declare const i: I<Json>;
takeN(i.m(x => { x; }, { v: 1 }, null!, 1, "a"));
// @filename: generated_075.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m<U>(p0?: Check<T>, p1?: Box<U>): T }
declare const i: I<Json>;
takeN(i.m());
// @filename: generated_076.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1: Check<T[]>, p2?: Box<T> | void, p3?: number): T;
const y: never = f<Json>("a", "a", 1, x => { x; }, (x: Json) => {});
// @filename: generated_077.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(): T;
declare function f<T = unknown>(p0: number, p1: Check<T[]>, p2: [T, Check<T>], p3?: Check<T> | void): T;
const x: Json = f({ s: "a" }, j);
// @filename: generated_078.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Opt<T>, p1?: Check<T>): T;
const x: Json = f<Json>({ a: 1 }, { s: "a" });
// @filename: generated_079.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1: Box<T>, p2: [T, Check<T>], p3: Opt<T>): T;
declare function f<T = unknown>(p0: Box<T> | void, p1: (x: T) => void): T;
const x: Json = f("a", { v: 1 }, j, 1);
// @filename: generated_080.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T[]>, p1: T, p2?: T): T }
declare const i: I<Json>;
const y: never = i.m({ v: "a" });
// @filename: generated_081.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Check<T[]>, p1: T, p2?: T): T }
declare const i: I<Json>;
const y: never = i.m(1);
// @filename: generated_082.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Check<T[]>): T;
const y: never = f<Json>();
// @filename: generated_083.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T>, p1: (x: T) => void, p2?: Check<T>, ...r: Box<T>[]): T;
declare function f<T = unknown>(p0?: T | void, p1?: Check<T>): T;
const y: never = f({ s: "a" }, (x: Json) => {}, { v: 1 });
// @filename: generated_084.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: (x: T) => void, p1?: Check<T>, p2?: Opt<T>); value: T }
const x: Json = new C<Json>({ a: 1 }, "a", j).value;
// @filename: generated_085.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T> | void, p1?: Box<Check<T>>): T;
declare function f<T = unknown>(p0: Check<T>, p1: (x: T) => void): T;
const x: Json = f(undefined);
// @filename: generated_086.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: Box<T>, p1?: Check<T[]>); value: T }
take(new C<Json>((x: Json) => {}, []).value);
// @filename: generated_087.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: Box<Check<T>>, ...r: T[]); value: T }
takeN(new C<Json>().value);
// @filename: generated_088.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: T | void, p1?: Check<T[]>): T;
const y: never = f<Json>(1, { s: "a" });
// @filename: generated_089.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: [T, Check<T>]): T;
const x: Json = f();
// @filename: generated_090.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: [T, Check<T>]): T;
takeN(f<Json>());
// @filename: generated_091.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Box<Check<T>>, p1?: Box<T>, p2?: [T, Check<T>]): T }
declare const i: I<Json>;
i.m(j, x => { x; }, "a", null!);
// @filename: generated_092.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Box<Check<T>>): T;
take(f());
// @filename: generated_093.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Box<Check<T>>): T;
f<Json>();
// @filename: generated_094.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Box<T>, p1: Box<T>, p2: Check<T[]>, p3: Opt<T>): T }
declare const i: I<Json>;
takeN(i.m({ v: "a" }, undefined, x => { x; }, []));
// @filename: generated_095.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Box<T>, p1: Box<T>, p2: Check<T[]>, p3: Opt<T>): T }
declare const i: I<Json>;
take(i.m({ s: "a" }, 1, { a: 1 }, "a"));
// @filename: generated_096.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0: Box<T>, p1: Box<T>, p2: Check<T[]>, p3: Opt<T>): T }
declare const i: I<Json>;
i.m((x: Json) => {}, { v: "a" }, 1);
// @filename: generated_097.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T | void, p1: Check<T>, p2: Box<T>): T;
const y: never = f<Json>([], []);
// @filename: generated_098.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: number, p1?: Check<T[]>, p2?: Check<T>, p3?: Opt<T>); value: T }
new C<Json>(x => { x; }, { s: "a" }, x => { x; }).value;
// @filename: generated_099.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: number, p1?: T | void, p2?: Check<T> | void, p3?: Check<T> | void): T;
take(f<Json>(1, { a: 1 }, 1, { s: "a" }, null!));
// @filename: generated_100.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: Box<T>, p1?: Check<T[]>, p2?: [T, Check<T>]); value: T }
take(new C({ v: "a" }, { a: 1 }, j, { s: "a" }).value);
// @filename: generated_101.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: Box<T>, p1?: Check<T[]>, p2?: [T, Check<T>]); value: T }
const y: never = new C<Json>({ a: 1 }, x => { x; }).value;
// @filename: generated_102.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: (x: T) => void, p1?: Check<T>): T;
const x: Json = f<Json>(undefined);
// @filename: generated_103.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T> | void, p1?: number, p2?: Check<T[]>, p3?: (x: T) => void): T;
takeN(f<Json>({ a: 1 }, { s: "a" }, { s: "a" }, 1, { v: "a" }));
// @filename: generated_104.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T | void, p1?: Box<Check<T>>): T }
declare const i: I<Json>;
take(i.m());
// @filename: generated_105.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T | void, p1?: Box<Check<T>>): T }
declare const i: I<Json>;
takeN(i.m((x: Json) => {}, []));
// @filename: generated_106.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(): T;
declare function f<T = unknown>(p0: (x: T) => void, p1: Opt<T>, p2?: Check<T[]>, p3?: (x: T) => void): T;
const y: never = f((x: Json) => {}, undefined, { v: 1 });
// @filename: generated_107.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1?: Check<T>, p2?: Box<T>, p3?: Check<T>): T;
take(f<Json>((x: Json) => {}, { v: "a" }, j, { v: 1 }));
// @filename: generated_108.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Check<T>): T;
declare function f<T = unknown>(p0?: Opt<T>): T;
take(f());
// @filename: generated_109.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: T, p1?: (x: T) => void, p2?: Check<T>, p3?: Check<T>); value: T }
take(new C(null!, (x: Json) => {}, null!, undefined, { v: "a" }).value);
// @filename: generated_110.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0?: T, p1?: (x: T) => void, p2?: Check<T>, p3?: Check<T>); value: T }
new C<Json>("a", { s: "a" }, x => { x; }).value;
// @filename: generated_111.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: Check<T> | void, p1?: Check<T[]>); value: T }
take(new C<Json>().value);
// @filename: generated_112.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Opt<T>, p1?: Check<T> | void, p2?: Check<T>): T;
take(f({ v: 1 }));
// @filename: generated_113.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Opt<T>, p1?: Check<T> | void, p2?: Check<T>): T;
takeN(f<Json>(x => { x; }));
// @filename: generated_114.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1: number, p2?: number, p3?: Check<T[]>, ...r: T[]): T;
takeN(f(j, null!));
// @filename: generated_115.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: T, p1: number, p2?: number, p3?: Check<T[]>, ...r: T[]): T;
const x: Json = f<Json>({ v: "a" }, x => { x; }, []);
// @filename: generated_116.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: number, p1?: (x: T) => void, p2?: Check<T> | void, p3?: [T, Check<T>]): T }
declare const i: I<Json>;
take(i.m({ v: "a" }, j, { v: "a" }));
// @filename: generated_117.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: number, p1?: (x: T) => void, p2?: Check<T> | void, p3?: [T, Check<T>]): T }
declare const i: I<Json>;
takeN(i.m({ v: 1 }, null!, [], x => { x; }));
// @filename: generated_118.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: number, p1?: (x: T) => void, p2?: Check<T> | void, p3?: [T, Check<T>]): T }
declare const i: I<Json>;
takeN(i.m((x: Json) => {}, { a: 1 }, 1, 1));
// @filename: generated_119.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T> | void): T;
declare function f<T = unknown>(p0?: Box<T> | void, p1?: Box<Check<T>>, p2?: T): T;
takeN(f("a", null!, j));
// @filename: generated_120.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: Check<T>, p1?: Check<T> | void, p2?: Box<T> | void, p3?: T): T }
declare const i: I<Json>;
take(i.m(j, x => { x; }, null!, [], (x: Json) => {}));
// @filename: generated_121.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T> | void): T;
takeN(f<Json>());
// @filename: generated_122.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T | void, p1?: Check<T> | void, p2?: Box<T> | void, p3?: (x: T) => void): T }
declare const i: I<Json>;
const y: never = i.m(1, "a", "a", j, "a");
// @filename: generated_123.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T | void, p1?: Check<T> | void, p2?: Box<T> | void, p3?: (x: T) => void): T }
declare const i: I<Json>;
takeN(i.m(x => { x; }, { v: "a" }, 1));
// @filename: generated_124.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
interface I<T> { m(p0?: T | void, p1?: Check<T> | void, p2?: Box<T> | void, p3?: (x: T) => void): T }
declare const i: I<Json>;
take(i.m(1, "a", { a: 1 }, { v: 1 }));
// @filename: generated_125.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Opt<T>, p1: Box<Check<T>>): T;
f<Json>(j, (x: Json) => {});
// @filename: generated_126.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0?: Box<T> | void, p1?: Box<T>, p2?: Box<Check<T>>): T;
declare function f<T = unknown>(p0: Check<T[]>, p1?: Check<T>): T;
const x: Json = f((x: Json) => {}, { s: "a" }, undefined);
// @filename: generated_127.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T> | void): T;
declare function f<T = unknown>(p0?: Box<Check<T>>, ...r: [a?: T, b?: Check<T>]): T;
const x: Json = f();
// @filename: generated_128.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T> | void, p1: Opt<T>, p2: Check<T> | void): T;
take(f("a", j));
// @filename: generated_129.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Box<T> | void, p1: Opt<T>, p2: Check<T> | void): T;
const x: Json = f<Json>(undefined, []);
// @filename: generated_130.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare function f<T = unknown>(p0: Check<T[]>): T;
const x: Json = f<Json>(1, x => { x; });
// @filename: generated_131.ts
export {}
type Json = string | Json[];
type ValidateJSON<T> = ((...args: Array<any>) => any) extends T ? never : { [K in keyof T]: ValidateJSON<T[K]> };
type Check<T> = T extends ValidateJSON<T> ? { a?: 1 } : { a?: 2 };
type Box<T> = { v: T };
type Opt<T> = T extends string ? { s: T } : { n: T };
declare function take(data: Json): void;
declare function takeN(data: number): void;
declare const j: Json;
declare class C<T = unknown> { constructor(p0: (x: T) => void, p1: Check<T[]>, ...r: T[]); value: T }
new C<Json>(1, j).value;
