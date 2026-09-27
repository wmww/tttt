// @exactOptionalPropertyTypes: true
// @strict: true
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type A = Partial<Record<string, number>>;
type B = Partial<Record<string, number | undefined>>;
type C = Prettify<Partial<Record<string, number | undefined>> & Required<Omit<Record<string, number | undefined>, never>>>;
declare const a: A; const a1: null = a;
declare const b: B; const b1: null = b;
declare const c: C; const c1: null = c;
declare const e: Partial<Record<string, number>>[string]; const e1: null = e;
declare const f: Partial<Record<string, number>>; const f1: null = f["x"];
type M<T> = { [K in keyof T]?: T[K] };
declare const g: M<{[k: string]: number}>; const g1: null = g["x"];
type X = Partial<Record<string, number>> & Required<Omit<Record<string, number>, never>>;
declare const x: X[string]; const x1: null = x;
declare const y: Prettify<X>; const y1: null = y;
declare const z: Prettify<X>[string]; const z1: null = z;
type Y = Partial<Record<string, number>> & Record<string, number>;
declare const w: Y[string]; const w1: null = w;
declare const v: Required<Omit<Record<string, number>, never>>; const v1: null = v;
