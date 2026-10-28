// The static side of a class whose base constructor is `X & K` (K a type variable) is `typeof C & K` (tsc's
// getBaseTypeVariableOfClass), what TS2417 relates to the base's static side
type Ctor = new (...args: any[]) => {};
declare class Base { static s: number; static t: string; }
function mix<K extends Ctor>(k: K) {
    const B = Base as typeof Base & K;
    class C extends B {
        static s = 1;
    }
    class D extends B {
        static s = "no";
    }
    class E extends B {
        static t = 2;
    }
    return [C, D, E];
}
function mix2<K extends Ctor, L extends Ctor>(k: K, l: L) {
    const B = Base as typeof Base & K & L;
    class F extends B {
        static s = "no";
    }
    return F;
}
