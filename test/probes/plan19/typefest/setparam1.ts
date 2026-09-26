// @strict: true
type M<A extends readonly unknown[], O> = O extends object ? { [K in keyof A]: K extends `${infer N extends number}` ? N extends keyof O ? O[N] : A[K] : A[K] } : never;
type R = M<[number, string], {1: boolean}>;
declare const r: R;
const x: [number, boolean] = r;
type S<F extends (...a: any[]) => unknown> = F extends (...a: infer P) => unknown ? M<P, {1: boolean}> : never;
declare const s: S<(a: number, b: string) => null>;
const y: [number, boolean] = s;
