// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "foo";
class C1 { get foo() { return 1; } set [k](v: number) {} }
new C1().foo = 2;
class C3 { static get foo() { return 1; } static set [k](v: number) {} }
C3.foo = 2;
class C6 { set [k](v: number) {} get foo() { return 1; } }
new C6().foo = 2;
declare const v5: { get foo(): 1; set [k](v: 2) }; v5.foo = 2;
interface I7 { get foo(): 1; set [k](v: 2) } declare const i7: I7; i7.foo = 2;
declare const v7: { foo: number; get [k](): string }; const n7: null = v7; v7.foo = "";
class C2 { get [k]() { return 1; } set foo(v: number) {} }
new C2().foo = 2;
class C8 { get [k]() { return 1; } set [k](v: number) {} }
new C8().foo = 2;
const sentinel: number = "";
