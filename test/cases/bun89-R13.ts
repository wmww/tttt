// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
// 1 an independent late name first: it starts the binding (getLateBoundSymbol), the key after it is typed in the meantime
declare const j1: "x";
class C1 { a: "b" = "b"; [j1]: string = ""; [k1]: number = 1 }
declare const k1: C1["a"];
new C1().x; new C1().b; new C1().a;
// 2 the circular key first
declare const j2: "x";
class C2 { a: "b" = "b"; [k2]: number = 1; [j2]: string = "" }
declare const k2: C2["a"];
new C2().x; new C2().b; new C2().a;
// 3 unique symbol first
declare const s3: unique symbol;
class C3 { a: "b" = "b"; [s3]: string = ""; [k3]: number = 1 }
declare const k3: C3["a"];
new C3()[s3]; new C3().b;
// 4 Symbol.iterator first
class C4 { a: "b" = "b"; *[Symbol.iterator]() { yield 1; } [k4]: number = 1 }
declare const k4: C4["a"];
for (const x of new C4()) {} new C4().b;
// 5 static side
declare const j5: "x";
class C5 { static a: "b" = "b"; static [j5]: string = ""; static [k5]: number = 1 }
declare const k5: (typeof C5)["a"];
C5.x; C5.b;
// 6 interface
declare const j6: "x";
interface I6 { a: "b"; [j6]: string; [k6]: number }
declare const k6: I6["a"];
declare const i6: I6; i6.x; i6.b;
// 7 declare class
declare const j7: "x";
declare class C7 { a: "b"; [j7]: string; [k7]: number }
declare const k7: C7["a"];
new C7().x; new C7().b;
// 8 subclass sees it?
declare const j8: "x";
class C8 { a: "b" = "b"; [j8]: string = ""; [k8]: number = 1 }
declare const k8: C8["a"];
class D8 extends C8 {}
new D8().x; new D8().b;
// 9 generic class, other instantiation
declare const j9: "x";
class C9<T> { a: "b" = "b"; [j9]: string = ""; [k9]: number = 1; t!: T }
declare const k9: C9<1>["a"];
new C9<1>().x; new C9<2>().x; new C9<2>().b;
// 10 type of `this` inside
declare const j10: "x";
class C10 { a: "b" = "b"; [j10]: string = ""; [k10]: number = 1; m() { this.x; this.b; } }
declare const k10: C10["a"];
const sentinel: number = "";
