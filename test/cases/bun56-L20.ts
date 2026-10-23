// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class B<T = 1> {} declare class C {} declare class D {} interface I<T = 1> {} declare const a: any;
class A1 extends B, class extends C, D {} {}
class A2 implements I implements (class extends C, D {}) {}
class A3 extends B extends class extends C, D {} {}
class A4 extends B, class<> {} {}
class A5 implements (class extends C, D {}) {}
interface A6 extends I extends (class extends C, D {}) {}
interface A7 extends (class extends C, D {}) {}
public class A8 extends B, class extends C, D {} {}
class A9 extends B, (a?.b`x`) {}
class A10 extends B, (0 as I<>) {}
class A11 extends B, (a<>()) {}
class A12 extends B, (import<string>) {}
class A13 extends B, ((x: I<1,>) => 1) {}
class A14 extends B, (function (p?, q) {}) {}
const sentinel: number = "";
