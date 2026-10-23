// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
class Base<T> { value?: T; other!: T; n?: number; init: T | undefined = undefined; }
type Ctor<T> = new (...a: any[]) => T;
function MixAny<TB extends Ctor<Base<any>>>(B: TB) { return class extends B { extra = 1 }; }
function MixUnknown<TB extends Ctor<Base<unknown>>>(B: TB) { return class extends B {}; }
function MixSame<TB extends Ctor<Base<number>>>(B: TB) { return class extends B {}; }
class D1 extends MixAny(Base<number>) {}
class Num extends Base<number> {}
class D2 extends MixAny(Num) {}
class D3 extends MixUnknown(Base<number>) {}
class D4 extends MixSame(Base<number>) {}
class D5 extends MixAny(Base<number>) { value?: number }
const sentinel: number = "";
