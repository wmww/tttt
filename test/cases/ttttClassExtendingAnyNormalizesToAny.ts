// @target: esnext
// A class that declares no instance members over a base of any (a constructor of any, or one that constructs any) is
// any where it's related: tsc's getBaseTypes is [any] and getNormalizedType takes a non-augmenting subtype's single base
// (getSingleBaseForNonAugmentingSubtype). With members or a constructor it's compared structurally (mutation testing,
// mixinAbstractClassesReturnTypeInference without its mixinMethod)
interface I { m(): void }
declare const B: any;
class C extends B {}
declare const c: C;
const i1: I = c;
class D extends B { x = 1 }
const i2: I = new D();
class E extends B { constructor() { super(); } }
const i3: I = new E();
class F extends B implements I { static s(): void {} }
function mix<T extends abstract new (...args: any[]) => any>(b: T) {
    abstract class M extends b implements I {}
    return M;
}
declare const ctor: new () => any;
class G extends ctor implements I {}
