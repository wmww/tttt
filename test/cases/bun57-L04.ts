// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: any;
declare class A<T> {}
interface J<T> {}
class C2 extends A<1>, A<{ 2n: 1 }> {}
interface I2 implements J<{ 1n: 1 }> {}
class C3 extends A<1> extends A<{ 1n: 1 }> {}
class C4 implements J<1> implements J<{ 1n: 1 }> {}
interface I3 extends J<1> extends J<{ 1n: 1 }> {}
class C5 extends A<1>, (class { 1n = 1 }) {}
with (o) { var v: { 1n: 1 }; var w = <{ 1n: 1 }>o; }
v; w;
const sentinel: number = "";
