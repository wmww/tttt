// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class A<T> { abstract m(): void; abstract p: number; abstract h: T; abstract o: { a: 1 }; abstract arr: number[]; }
function mixN<T extends abstract new (...a: any[]) => A<number>>(B: T) { abstract class M extends B {} return M; }
function mixA<T extends abstract new (...a: any[]) => A<any>>(B: T) { abstract class M extends B {} return M; }
class N1 extends mixN(A<number>) {}
class A1 extends mixA(A<number>) {}
class AA extends mixA(A<any>) {}
abstract class A2<T> { abstract m(): void; }
function mix2<T extends abstract new (...a: any[]) => A2<any>>(B: T) { abstract class M extends B {} return M; }
class E2 extends mix2(A2<number>) {}
const sentinel: number = "";
