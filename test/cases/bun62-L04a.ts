// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "foo";
class C1 { get foo() { return 1 } set [k](v: number) {} }
new C1().foo = 2;
class C2 { static get foo() { return 1 } static set [k](v: number) {} }
C2.foo = 2;
interface I3 { get foo(): number; set [k](v: number) }
declare const i3: I3; i3.foo = 2;
type T4 = { get foo(): number; set [k](v: number) };
declare const t4: T4; t4.foo = 2;
class C6 { set [k](v: number) {} get foo() { return 1 } }
new C6().foo = 2;
class C9 { get foo(): number { return 1 } set [k](v) {} }
new C9().foo = "";
type R1 = { [P in keyof C1]: C1[P] };
declare const m1: R1; m1.foo = 1;
const sentinel: number = "";
