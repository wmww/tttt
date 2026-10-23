// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const { a, ...rest } = { a: 1, b: 2, c: 3 };
declare function f<T>(x: typeof rest, y: T): T;
declare function f(x: { b: string }, y: number): number;
class C {
  b = "";
  c = f(this, 1);
}
const sentinel: number = "";
