// An expando assignment in a class decorator, a member decorator or a member computed name isn't bound (bun case
// 30-C3): tsc's binder looks the name up in the container there, the class or the method, without f
// @strict: true
// @experimentalDecorators: true
function f() {}
@(f.a = 1 as any) class C {}
class D {
    @(f.b = 1 as any) p = 1;
    @(f.c = 1 as any) m() {}
    [f.d = "k"] = 2;
    [f.e = "j"]() {}
}
@((() => { f.g = 1; }) as any) class E {}
f.a; f.b; f.c; f.d; f.e; f.g;
f.h = 1;
f.h;
