// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<A extends unknown[]> = [...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A, ...A];
const x1 = f1();
const x2 = new C2();
async function g() {
  const x3 = await f3();
  const x4 = await (0 as any as X<X<X<X<[4]>>>>);
}
function h(p = f5()) {}
class K { q = f6(); }
const { a = f7() } = {} as { a?: number };
const x9 = (f9());
declare function f1(): X<X<X<X<[1]>>>>;
declare const C2: new () => X<X<X<X<[2]>>>>;
declare function f3(): X<X<X<X<[3]>>>>;
declare function f5(): X<X<X<X<[5]>>>>;
declare function f6(): X<X<X<X<[6]>>>>;
declare function f7(): X<X<X<X<[7]>>>>;
declare function f9(): X<X<X<X<[9]>>>>;
const sentinel: number = "";
