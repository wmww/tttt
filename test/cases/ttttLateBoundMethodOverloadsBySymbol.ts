// @target: es2017
// Late-bound method declarations are one method by their symbol, not their text: [a] and [b] with b of typeof a
// overload each other, the implementation after [b] is [a]'s
declare const a: unique symbol;
declare const b: typeof a;
class C {
    [a](x: number): void;
    [b](x: string): void;
    [a](x: any) {}
}
const c = new C();
const r1 = c[a](1);
const r2 = c[a]("s");
type M = C[typeof a];
class D {
    get [a](): number { return 1; }
    set [b](v: string) {}
}
const g = new D()[a];
new D()[a] = "x";
