// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I<T> { a: T extends string ? number : number }
interface I<T> { b: number }
function g<T>(o: I<T>, x: number) { o.a = x; }
const sentinel: number = "";
