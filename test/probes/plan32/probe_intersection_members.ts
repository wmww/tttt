declare class C1 {
    public a: number;
    constructor(s: string);
    constructor(n: number);
}
declare class M1 {
    constructor(...args: any[]);
    p: number;
    static p: number;
}
declare class M2 {
    constructor(...args: any[]);
    f(): number;
}
declare const Mixed1: typeof M1 & typeof C1;
declare const Mixed3: typeof M2 & typeof M1 & typeof C1;
declare const Mixed5: typeof M1 & typeof M2;
const x1 = new Mixed1("hello");
x1.a; x1.p;
const x3 = new Mixed3(42);
x3.a; x3.p; x3.f();
const x5 = new Mixed5();
x5.p; x5.f();
class D extends Mixed1 {
    constructor() { super("hello"); this.a; this.p; }
}
declare const both: { [k: string]: number } & { [k: string]: 1 | 2 };
const e = both["x"];
