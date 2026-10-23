// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": []}, "files": ["a.ts"]}
// @filename: a.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends { x: Comp<any> } ? (C['x'] extends new () => 0 ? C : never) : never;
type Enh<I, N> = <C extends { x: Comp<GetProps<C>> }>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends { x: Comp<any> }>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
type F<T> = T extends { a: unknown } ? (T['a'] extends string ? 1 : 2) : 3;
export const f1: never = null! as F<{ a: 'x' }>;
export const f2: never = null! as F<{ a: 1 }>;
function g<T extends { a: unknown }>(t: T) { const x: never = null! as F<T>; }
export const sentinel: number = '';
