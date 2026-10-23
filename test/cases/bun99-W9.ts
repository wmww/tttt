// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": []}, "files": ["lib.ts", "a.ts", "b.ts"]}
// @filename: a.ts
import { flow, hoc, enh } from './lib';
export const R1 = flow(hoc, enh);
export const R2 = flow(hoc, enh);
export const R3 = flow(enh, enh);
// @filename: b.ts
import { flow, hoc, enh } from './lib';
export const R4 = flow(hoc, enh);
// @ts-expect-error
export const R5 = flow(hoc, enh);
// @filename: lib.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
export type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
export declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
export declare const hoc: <C extends Comp<any>>(c: C) => C;
export declare const enh: Enh<{}, {}>;
