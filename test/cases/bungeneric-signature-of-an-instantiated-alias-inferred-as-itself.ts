// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "es2022", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
type Id<A> = <T extends A>(a: T) => T;
type IdS = <T extends string>(a: T) => T;
type Id0<A> = <T>(a: T, b: A) => T;
interface IdI<A> { <T extends A>(a: T): T }
declare function wrap<R, P extends any[]>(f: (...a: P) => R): (...a: P) => R;
declare function wrap1<R, X>(f: (a: X) => R): (a: X) => R;
declare const id: Id<string>;
declare const ids: IdS;
declare const id0: Id0<string>;
declare const idi: IdI<string>;
export const a1: Id<string> = wrap(id);
export const a2: IdS = wrap(ids);
export const a3: Id<string> = wrap(ids);
export const a4: IdS = wrap(id);
export const a5: Id0<string> = wrap(id0);
export const a6: IdI<string> = wrap(idi);
export const a7: Id<string> = wrap1(id);
export const a8 = wrap(id);
export const a9: 0 = wrap(id);
export const b1: Id<number> = wrap(id);
export const b2: Id<"a"> = wrap(id);
export function g<B extends string>(f: Id<B>): Id<B> { return wrap(f); }
const sentinel: number = "";
