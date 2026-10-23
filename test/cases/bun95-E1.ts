// @filename: tsconfig.json
{
 "compilerOptions": {
  "strict": true,
  "noEmit": true,
  "target": "esnext",
  "module": "esnext",
  "moduleResolution": "bundler",
  "lib": [
   "esnext"
  ],
  "types": [],
  "skipLibCheck": true
 }
}
// @filename: a.ts
interface Variance<E> { readonly _E: (_: never) => E }
interface Yieldable { readonly id: Variance<this>; }
type S<F> = { readonly [K in keyof F]: F[K] };
declare function own<F>(f: F): new () => S<F> & Yieldable;
declare function lib<F>(f: F): new () => Readonly<F> & Yieldable;
declare function pick<F>(f: F): new () => Pick<F, keyof F> & Yieldable;
declare function self<Self>(): <F>(f: F) => new () => S<F> & Yieldable;
export class A extends own({ _tag: "A" as const }) {}
export class B extends lib({ _tag: "B" as const }) {}
export class C extends pick({ _tag: "C" as const }) {}
export class D extends self<D>()({ _tag: "D" as const }) {}
declare const a: A;
// `this` of the inherited member is the class. In a value of the base type it is the intersection.
const n1: number = a.id;
const n2: number = new (own({ _tag: "A" as const }))().id;
export class E extends own({ _tag: "E" as const }) {
  m(): Variance<this> {
    return this.id;
  }
}
const sentinel: number = "";
