// @strict: true
type N<A extends readonly unknown[], O> = O extends object ? { [K in keyof A]: A[K] } : never;
declare const b: N<[number, string], {}>;
declare const c: N<[number, ...string[]], {}>;
type Q<A extends readonly unknown[], O> = O extends object ? { [K in keyof A]: A[K] } : { [K in keyof A]: A[K] };
declare const d: Q<[number, ...string[]], {}>;
type N2<A extends readonly unknown[], O> = { x: 1 } extends O ? { [K in keyof A]: A[K] } : never;
declare const e: N2<[number, ...string[]], {}>;
type N3<A extends readonly unknown[]> = { [K in keyof A]: A[K] };
declare const f: N3<[number, ...string[]]>;
