// @strict: true

// A type query (`typeof` written as a type) is no use for definite assignment: `typeof this.x` is a QualifiedName,
// not the `this.x` access that is undefined until assigned in the constructor (TS2565), and `typeof x` of an
// unassigned variable isn't TS2454. Both are still narrowed by the flow.
class Foo {
    foo: number;
    bar: string | number;
    baz: string | undefined;
    constructor() {
        this.foo = 0 as typeof this.foo;
        let a: typeof this.bar = 1;
        this.bar = "s";
        let b: typeof this.bar = "t";
        let c: typeof this.baz = undefined;
        this.baz = "u";
        const d = "v" as typeof this.baz;
        this.foo;
    }
}

class Used {
    p: number;
    constructor() {
        this.p;
        this.p = 1;
    }
}

function f(p: string | number, q?: string) {
    let x: number;
    const y = 0 as typeof x;
    let z: typeof x = 1;
    const c: string;
    let w: typeof c = "";
    if (typeof p === "string") {
        let s: typeof p = p;
        const n = p as typeof p;
    }
    let u: string | undefined;
    let v: typeof u;
    u = "a";
    let t: typeof u = u;
    let r: typeof q = q;
    if (q) {
        let r2: typeof q = q;
    }
    x;
}
