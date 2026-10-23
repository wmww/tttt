// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "lib": ["esnext"], "types": []}, "files": ["again_cond__i_first.ts", "again_generic__i_first.ts", "again_pick__i_first.ts", "again_prop__i_first.ts", "again_prop_opt__i_first.ts", "again_two__i_first.ts", "both_ro__alias__call.ts", "both_ro__alias__context.ts", "both_ro__alias__from_index.ts", "both_ro__alias__from_obj.ts", "both_ro__alias__identical.ts", "both_ro__alias__in.ts", "both_ro__alias__infer_index.ts", "both_ro__alias__infer_sig.ts", "both_ro__alias__keyof.ts", "both_ro__alias__mapped.ts", "both_ro__alias__mapped_as.ts", "both_ro__alias__mapped_ro.ts", "both_ro__alias__mapped_rw.ts", "both_ro__alias__new.ts", "both_ro__alias__params.ts", "both_ro__alias__partial.ts", "both_ro__alias__pattern.ts", "both_ro__alias__read.ts", "both_ro__alias__rest.ts", "both_ro__alias__reverse.ts", "both_ro__alias__reverse_box.ts", "both_ro__alias__reverse_opt.ts", "both_ro__alias__reverse_ro.ts", "both_ro__alias__spread.ts", "both_ro__alias__to_index.ts", "both_ro__alias__to_mapped.ts", "both_ro__alias__write.ts", "both_ro__constraint__reverse.ts", "both_ro__constraint__reverse_box.ts", "both_ro__constraint__reverse_opt.ts", "both_ro__constraint__reverse_ro.ts", "both_ro__constraint__to_mapped.ts", "both_ro__itself__mapped.ts", "both_ro__itself__mapped_as.ts", "both_ro__itself__partial.ts", "both_ro__itself__reverse.ts", "both_ro__itself__reverse_box.ts", "both_ro__itself__reverse_opt.ts", "both_ro__itself__reverse_ro.ts", "both_ro__itself__to_mapped.ts", "both_ro__property__mapped.ts", "both_ro__property__mapped_as.ts", "both_ro__property__partial.ts", "both_ro__property__reverse.ts", "both_ro__property__reverse_box.ts", "both_ro__property__reverse_opt.ts", "both_ro__property__reverse_ro.ts", "both_ro__property__to_mapped.ts", "ctor__alias__reverse.ts", "ctor__constraint__new.ts", "only_index__alias__reverse.ts", "overloads__alias__call.ts", "overloads__constraint__call.ts", "record__alias__iface.ts", "record__alias__reverse.ts", "record__alias__reverse_box.ts", "record__alias__reverse_opt.ts", "record__alias__reverse_ro.ts", "record__alias__to_mapped.ts", "record__itself__iface.ts", "ro_string__alias__mapped.ts", "ro_string__constraint__spread.ts", "same_key__alias__reverse.ts", "same_sig__alias__reverse.ts", "sub_alias_arg.ts", "sub_constraint.ts", "sub_constraint_bad.ts", "sub_ctor.ts", "sub_index.ts", "sub_infer.ts", "sub_mapped.ts", "sub_nested.ts", "sub_params.ts", "sub_ret.ts", "sub_template.ts", "sub_tuple.ts", "sub_typeof.ts", "symbol__alias__to_mapped.ts", "template__alias__mapped.ts", "two_keys__alias__mapped.ts", "two_keys__constraint__spread.ts"]}
// @filename: again_cond__i_first.ts
interface Box { v: Box & (keyof Box['v'] extends infer K ? { k: K } : never); w: 1 }
type I = Box['v'];
type M = keyof I;
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: again_generic__i_first.ts
interface Box<T> { v: T & Box<T> & { [K in keyof Box<T>['v']]: K }; w: 1 }
type I = Box<{ t: 1 }>['v'];
type M = { [K in keyof I]: K };
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: again_pick__i_first.ts
interface Box { v: Box & Pick<Box['v'], keyof Box['v']>; w: 1 }
type I = Box['v'];
type M = Pick<I, keyof I>;
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: again_prop__i_first.ts
interface Box { v: Box & { [K in keyof Box['v']]: K }; w: 1 }
type I = Box['v'];
type M = { [K in keyof Box['v']]: K };
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: again_prop_opt__i_first.ts
interface Box { v: Box & { [K in keyof Box['v']]?: K } & { x: 1 }; w: 1 }
type I = Box['v'];
type M = { [K in keyof Box['v']]?: K };
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: again_two__i_first.ts
interface A { b: B['a'] & { [K in keyof A['b']]: 1 }; p: 1 }
interface B { a: A['b'] & { [K in keyof B['a']]: 2 }; q: 2 }
type I = A['b'];
type M = B['a'];
const a: never = null! as keyof I;
const b: never = null! as keyof M;
const c: never = null! as I;
const d: never = (null! as I).v;
const e: never = (null! as I).w;
export const sentinel: number = "";
// @filename: both_ro__alias__call.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = (v as any as X)("s");
const q: never = (v as any as X)(1);
export const sentinel: number = "";
// @filename: both_ro__alias__context.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const r: X = { a: 1, xq: (y: any) => y } as any;
const c: { f: X } = { f: { a: 1, xq: 1, zz: "s" } };
export const sentinel: number = "";
// @filename: both_ro__alias__from_index.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const r: X = null! as { [k: string]: 1 };
export const sentinel: number = "";
// @filename: both_ro__alias__from_obj.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const r: X = null! as { a: 1;
xq: 1 };
export const sentinel: number = "";
// @filename: both_ro__alias__identical.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare let m1: X;
declare let m1: { [K in keyof X]: X[K] };
export const sentinel: number = "";
// @filename: both_ro__alias__in.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
if ("xq" in v) { const p: never = v;
} else { const q: never = v;
}
export const sentinel: number = "";
// @filename: both_ro__alias__infer_index.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [k: string]: U }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__alias__infer_sig.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<A, R>(x: (a: A) => R): [A, R];
const p: never = g(v as any as X);
export const sentinel: number = "";
// @filename: both_ro__alias__keyof.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = null! as keyof X;
export const sentinel: number = "";
// @filename: both_ro__alias__mapped.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__mapped_as.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
type R = { [K in keyof X as K]: X[K] };
declare const r: R;
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__mapped_ro.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
type R = { readonly [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__mapped_rw.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
type R = { -readonly [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__new.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = new (v as any as X)("s");
const q: never = new (v as any as X)(1);
export const sentinel: number = "";
// @filename: both_ro__alias__params.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = null! as (X extends (...a: infer A) => infer R ? [A, R] : 0);
const q: never = null! as (X extends new (...a: infer A) => infer R ? [A, R] : 0);
export const sentinel: number = "";
// @filename: both_ro__alias__partial.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare const r: Partial<X>;
r["xq"] = null!;
const p: never = r["xq"];
export const sentinel: number = "";
// @filename: both_ro__alias__pattern.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const { xq, ...r } = v;
const p: never = xq;
const q: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__read.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = v["xq"];
const q: never = v[0];
export const sentinel: number = "";
// @filename: both_ro__alias__rest.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const { a, ...r } = v as X & { a?: unknown };
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__reverse.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__reverse_box.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] | { b: U[K] } }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__alias__reverse_opt.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]?: U[K] }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__alias__reverse_ro.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { readonly [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__spread.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const r = { ...v };
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__alias__to_index.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
const r: { [k: string]: number } = v;
const s: { readonly [k: string]: unknown } = v;
export const sentinel: number = "";
// @filename: both_ro__alias__to_mapped.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__alias__write.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = I;
type X0 = X;
declare const v: I;
v["xq"] = null!;
v[0] = null!;
export const sentinel: number = "";
// @filename: both_ro__constraint__reverse.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
export function f<T extends I>(v: T) {
  type X = T;
  const r = g(v);
  r["xq"] = null!;
  const p: never = r;
}
export const sentinel: number = "";
// @filename: both_ro__constraint__reverse_box.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]: U[K] | { b: U[K] } }): U;
export function f<T extends I>(v: T) {
  type X = T;
  const p: never = g(v);
}
export const sentinel: number = "";
// @filename: both_ro__constraint__reverse_opt.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]?: U[K] }): U;
export function f<T extends I>(v: T) {
  type X = T;
  const p: never = g(v);
}
export const sentinel: number = "";
// @filename: both_ro__constraint__reverse_ro.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { readonly [K in keyof U]: U[K] }): U;
export function f<T extends I>(v: T) {
  type X = T;
  const r = g(v);
  r["xq"] = null!;
  const p: never = r;
}
export const sentinel: number = "";
// @filename: both_ro__constraint__to_mapped.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
export function f<T extends I>(v: T) {
  type X = T;
  const p: never = g(v);
}
export const sentinel: number = "";
// @filename: both_ro__itself__mapped.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__itself__mapped_as.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type R = { [K in keyof X as K]: X[K] };
declare const r: R;
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__itself__partial.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare const r: Partial<X>;
r["xq"] = null!;
const p: never = r["xq"];
export const sentinel: number = "";
// @filename: both_ro__itself__reverse.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__itself__reverse_box.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]: U[K] | { b: U[K] } }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__itself__reverse_opt.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { [K in keyof U]?: U[K] }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__itself__reverse_ro.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<U>(x: { readonly [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__itself__to_mapped.ts

type X = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X0 = X;
declare const v: { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__property__mapped.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__property__mapped_as.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
type R = { [K in keyof X as K]: X[K] };
declare const r: R;
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__property__partial.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare const r: Partial<X>;
r["xq"] = null!;
const p: never = r["xq"];
export const sentinel: number = "";
// @filename: both_ro__property__reverse.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__property__reverse_box.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare function g<U>(x: { [K in keyof U]: U[K] | { b: U[K] } }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__property__reverse_opt.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare function g<U>(x: { [K in keyof U]?: U[K] }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: both_ro__property__reverse_ro.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare function g<U>(x: { readonly [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: both_ro__property__to_mapped.ts
type I = { readonly [k: string]: number } & { readonly [k: string]: 1 | 2 };
type X = O['p'];
type X0 = X;
type O = { p: I }; declare const v: O["p"];
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
const p: never = g(v);
export const sentinel: number = "";
// @filename: ctor__alias__reverse.ts
type I = { new (a: string): { x: 1 }; [k: string]: unknown } & { new (a: number): { y: 2 } };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: ctor__constraint__new.ts
type I = { new (a: string): { x: 1 }; [k: string]: unknown } & { new (a: number): { y: 2 } };
export function f<T extends I>(v: T) {
  type X = T;
  const p: never = new (v as any as X)("s");
  const q: never = new (v as any as X)(1);
}
export const sentinel: number = "";
// @filename: only_index__alias__reverse.ts
type I = { [k: string]: number } & { readonly [k: string]: 1 | 2 | 3 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: overloads__alias__call.ts
type I = { (a: string): 1; p: 1 } & { (a: number): 2; q: 2 };
type X = I;
type X0 = X;
declare const v: I;
const p: never = (v as any as X)("s");
const q: never = (v as any as X)(1);
export const sentinel: number = "";
// @filename: overloads__constraint__call.ts
type I = { (a: string): 1; p: 1 } & { (a: number): 2; q: 2 };
export function f<T extends I>(v: T) {
  type X = T;
  const p: never = (v as any as X)("s");
  const q: never = (v as any as X)(1);
}
export const sentinel: number = "";
// @filename: record__alias__iface.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
interface J extends X0 { z: 1 } declare const j: J;
j["xq"] = null!;
const p: never = j["xq"];
export const sentinel: number = "";
// @filename: record__alias__reverse.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: record__alias__reverse_box.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] | { b: U[K] } }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: record__alias__reverse_opt.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]?: U[K] }): U;
const p: never = g(v);
export const sentinel: number = "";
// @filename: record__alias__reverse_ro.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { readonly [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: record__alias__to_mapped.ts
type I = Record<string, number> & { tag: string };
type X = I;
type X0 = X;
declare const v: I;
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
const p: never = g(v);
export const sentinel: number = "";
// @filename: record__itself__iface.ts

type X = Record<string, number> & { tag: string };
type X0 = X;
declare const v: Record<string, number> & { tag: string };
interface J extends X0 { z: 1 } declare const j: J;
j["xq"] = null!;
const p: never = j["xq"];
export const sentinel: number = "";
// @filename: ro_string__alias__mapped.ts
type I = { readonly [k: string]: number } & { a: 1 };
type X = I;
type X0 = X;
declare const v: I;
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: ro_string__constraint__spread.ts
type I = { readonly [k: string]: number } & { a: 1 };
export function f<T extends I>(v: T) {
  type X = T;
  const r = { ...v };
  r["xq"] = null!;
  const p: never = r;
}
export const sentinel: number = "";
// @filename: same_key__alias__reverse.ts
type I = { [k: string]: number | string } & { readonly [k: string]: number | boolean };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: same_sig__alias__reverse.ts
type I = { (a: string): 1; [k: string]: unknown } & { (a: string): 1; q: 2 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<U>(x: { [K in keyof U]: U[K] }): U;
const r = g(v);
r["xq"] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: sub_alias_arg.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type Box<F> = { f: F }
type R<T> = T extends Fn2 ? Box<T>['f'] : never
function f<T extends Fn2>(x: R<T>) { const r: never = x('s') }
export const sentinel: number = ""
// @filename: sub_constraint.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type Call<F extends (a: string) => unknown> = F
type New<F extends new (a: string) => unknown> = F
type Rec<F extends { [k: string]: unknown }> = F
type R<T> = T extends Fn2 ? Call<T> : T extends Ctor2 ? New<T> : T extends Idx ? Rec<T> : never
const a: never = null! as R<Fn2>
export const sentinel: number = ""
// @filename: sub_constraint_bad.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type Call<F extends (a: boolean) => unknown> = F
type Rec<F extends { [k: string]: string }> = F
type R<T> = T extends Fn2 ? Call<T> : T extends Idx ? Rec<T> : never
export const sentinel: number = ""
// @filename: sub_ctor.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Ctor2 ? [InstanceType<T>, ConstructorParameters<T>] : never
const a: never = null! as R<Ctor2>
function f<T>() { const b: never = null! as R<T> }
export const sentinel: number = ""
// @filename: sub_index.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Idx ? [T[string], T['a'], keyof T] : never
const a: never = null! as R<Idx>
function f<T>() { const b: never = null! as R<T> }
export const sentinel: number = ""
// @filename: sub_infer.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Obj ? (T extends (a: infer A) => infer B ? [A, B] : 0) : never
const a: never = null! as R<Obj>
function f<T>() { const b: never = null! as R<T> }
export const sentinel: number = ""
// @filename: sub_mapped.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Idx ? { [K in keyof T]: T[K] } : never
declare const a: R<Idx>; a['x'] = 1
function f<T>(b: R<T>) { const c: never = b }
export const sentinel: number = ""
// @filename: sub_nested.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Fn2 ? (T extends Obj ? [ReturnType<T>, T['p'], T[string]] : 1) : never
const a: never = null! as R<Fn2 & Obj>
export const sentinel: number = ""
// @filename: sub_params.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Fn2 ? Parameters<T> : never
const a: never = null! as R<Fn2>
function f<T>() { const b: never = null! as R<T> }
export const sentinel: number = ""
// @filename: sub_ret.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Fn2 ? ReturnType<T> : never
const a: never = null! as R<Fn2>
function f<T>() { const b: never = null! as R<T> }
export const sentinel: number = ""
// @filename: sub_template.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Idx ? `${T[string]}` : never
const a: never = null! as R<Idx>
export const sentinel: number = ""
// @filename: sub_tuple.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Idx ? [...T[string][]] : never
const a: never = null! as R<Idx>
export const sentinel: number = ""
// @filename: sub_typeof.ts
type Fn2 = ((a: string) => 1) & ((a: number) => 2)
type Ctor2 = (new (a: string) => { x: 1 }) & (new (a: number) => { y: 2 })
type Idx = { readonly [k: string]: number } & { a: 1 }
type Obj = { (a: string): 1; p: 1 } & { (a: number): 2; [k: string]: unknown }
type R<T> = T extends Fn2 ? { f: T; r: ReturnType<T> } : never
function f<T extends Fn2>(x: R<T>) { const r = x.f('s'); const q = x.f(1); const p: never = r; const s: never = q }
export const sentinel: number = ""
// @filename: symbol__alias__to_mapped.ts
type I = { readonly [k: symbol]: number } & { a: 1 };
type X = I;
type X0 = X;
declare const v: I;
declare function g<K extends string, V>(x: { [P in K]: V }): [K, V];
const p: never = g(v);
export const sentinel: number = "";
// @filename: template__alias__mapped.ts
type I = { readonly [k: `x${string}`]: number } & { a: 1 };
type X = I;
type X0 = X;
declare const v: I;
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: two_keys__alias__mapped.ts
type I = { readonly [k: string]: number | string } & { [k: number]: 1 };
type X = I;
type X0 = X;
declare const v: I;
type R = { [K in keyof X]: X[K] };
declare const r: R;
r["xq"] = null!;
r[0] = null!;
const p: never = r;
export const sentinel: number = "";
// @filename: two_keys__constraint__spread.ts
type I = { readonly [k: string]: number | string } & { [k: number]: 1 };
export function f<T extends I>(v: T) {
  type X = T;
  const r = { ...v };
  r["xq"] = null!;
  const p: never = r;
}
export const sentinel: number = "";
