// @strict: true
type F<P> = P extends `${infer I extends number}.${infer S}` ? [I, S] : 0;
type G<P> = P extends `${infer I}.${infer S}` ? [I, S] : 0;
declare const a: F<`${number}.a`>;
declare const b: G<`${number}.a`>;
declare const c: F<'0.a'>;
declare const d: F<'3.a'>;
type Arr = {a: 1; b: 2}[];
declare const e: NonNullable<Arr[number]>;
declare const f: Arr[3];
