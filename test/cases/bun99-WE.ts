// @filename: tsconfig.json
{"compilerOptions":{"strict":true,"noEmit":true,"target":"es2022","module":"esnext","types":[]},"files":["a.ts"]}
// @filename: a.ts
interface Box<X> {
  m<T, U extends T>(t: T, u: U): [X, T, U];
  n<T extends X, U extends T[]>(t: T, u: U): U;
}
declare const b: Box<number>;
declare function apply<U, R>(f: (x: U, y: U) => R): R;
declare function compose<U, B, C>(f: (x: U, y: U) => B, g: (b: B) => C): (x: U, y: U) => C;
declare function pipe2<T, U, R>(f: (t: T, u: U) => R): (t: T, u: U) => R;
export const r1 = apply(b.m);
export const r2 = compose(b.m, x => x);
export const r3 = pipe2(b.m);
export const r4 = pipe2(b.n);
export const r5 = apply(b.n);
const c1: never = r1;
const c2: never = r2;
const c3: never = r3;
const c4: never = r4;
const c5: never = r5;
r3(1, 2);
r3('a', 'b');
r4(1, [1]);
r4(1, ['']);
class K<X> {
  m<T extends X, U extends T>(t: T, u: U): U { return u; }
}
declare const k: K<string | number>;
export const r6 = pipe2(k.m);
const c6: never = r6;
r6('a', 'a');
r6('a', 1);
export const sentinel: number = '';
