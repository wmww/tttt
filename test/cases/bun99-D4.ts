// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "lib": ["esnext"], "types": []}, "files": ["both_never__cond__bool__assign.ts", "both_never__cond__bool__assign_obj.ts", "both_never__cond__bool__assign_wrong.ts", "both_never__cond__bool__context.ts", "both_never__cond__bool__eq.ts", "both_never__cond__bool__excess.ts", "both_never__cond__bool__switch.ts", "both_never__cond__bool__truthy.ts", "both_never__cond__num__assign.ts", "both_never__cond__str__assign.ts", "both_never__cond__tmpl__assign.ts", "both_never__cond_fn__bool__assign.ts", "both_never__obj_none__bool__assign.ts", "both_never__obj_same__bool__assign.ts", "both_never__obj_wide__bool__eq.ts", "both_never__param_obj__bool__assign.ts", "both_never__param_obj__param__context.ts", "both_never__param_obj__param__excess.ts", "both_never__param_obj__wide__context.ts", "both_never__param_obj__wide__excess.ts", "cond_never__cond__bool__assign.ts", "cond_obj__cond__bool__assign.ts", "cond_other__cond__bool__assign.ts", "conflict__param_obj__param__context.ts", "param__cond__bool__assign.ts", "param_never__cond__bool__assign.ts", "plain__cond__bool__assign.ts", "report.ts"]}
// @filename: both_never__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__assign_obj.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as { k: KT; o: 1; y: 1 };
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__assign_wrong.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A & { o: 2 };
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__context.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: true, f: x => { const p: never = x; } };
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__eq.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  if (u.k === true) { const p: never = u; } else { const q: never = u; }
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__excess.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: true, o: 1, zzz: 1 };
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__switch.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  switch (u.k) { case true: { const p: never = u; break } default: { const q: never = u; } }
}
export const sentinel: number = "";
// @filename: both_never__cond__bool__truthy.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  if (u.k) { const p: never = u; } else { const q: never = u; }
}
export const sentinel: number = "";
// @filename: both_never__cond__num__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = 1;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__cond__str__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = 'a';
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__cond__tmpl__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = `x${string}`;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__cond_fn__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & CO<T> & { f?: (x: number) => void });
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__obj_none__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & { o?: 1 });
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__obj_same__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & { k: KT; o?: 1 });
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__obj_wide__bool__eq.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & { k: unknown; o?: 1 });
  const u = null! as U;
  const t: never = u.k;
  if (u.k === true) { const p: never = u; } else { const q: never = u; }
}
export const sentinel: number = "";
// @filename: both_never__param_obj__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: both_never__param_obj__param__context.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = M;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: m, f: x => { const p: never = x; } };
}
export const sentinel: number = "";
// @filename: both_never__param_obj__param__excess.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = M;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: m, o: 1, zzz: 1 };
}
export const sentinel: number = "";
// @filename: both_never__param_obj__wide__context.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = string;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: 'a', f: x => { const p: never = x; } };
}
export const sentinel: number = "";
// @filename: both_never__param_obj__wide__excess.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = string;
  type A = { k: KT };
  type U = (A & CN<T> & N) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: 'a', o: 1, zzz: 1 };
}
export const sentinel: number = "";
// @filename: cond_never__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CN<T>) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: cond_obj__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CQ<T>) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: cond_other__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & CP<T>) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: conflict__param_obj__param__context.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = M;
  type A = { k: KT };
  type U = (A & B) | (A & P);
  const u = null! as U;
  const t: never = u.k;
  const r: U = { k: m, f: x => { const p: never = x; } };
}
export const sentinel: number = "";
// @filename: param__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & T) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: param_never__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & N) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: plain__cond__bool__assign.ts
type CN<T> = T extends infer E ? (E extends { k: infer K } ? { k: K } : never) : never
type CO<T> = T extends { k: infer K } ? { k: K; o?: 1 } : never
type CQ<T> = T extends { k: infer K } ? { k: K } : never
type CP<T> = T extends string ? { z: 1 } : never
export function f<T, N extends never, B extends { k: 'other' }, P extends { k: unknown; o?: 1 }, M extends string>(m: M) {
  type KT = true;
  type A = { k: KT };
  type U = (A & { y: 1 }) | (A & CO<T>);
  const u = null! as U;
  const t: never = u.k;
  const r: U = null! as A;
}
export const sentinel: number = "";
// @filename: report.ts
type Target<M extends 1> = {
  [K in 1]: {
    dataset: 'points'
    major: K
    variant: 'full' | 'commercial'
  }
}[M]

type Identity<C extends { watched: readonly unknown[] }> =
  C['watched'][number] extends infer E
    ? E extends { dataset: infer D; major: infer M; variant: infer V }
      ? { dataset: D; major: M; variant: V }
      : never
    : never

type Selector<I> =
  | I
  | (I extends { dataset: infer D; major: infer M; variant: infer V }
      ? 'full' extends V
        ? { dataset: D; major: M; variant?: V }
        : never
      : never)

function test<M extends 1>(target: Target<M>) {
  const result: { dataset: 'points'; major: M } &
    Selector<Identity<{ watched: readonly [Target<M>] }>> = target
  return result
}
