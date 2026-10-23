// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type IsPrim<T> = T extends string | number ? true : false;
interface Base<F> { f: F }
declare let a: A;
let u: { x: number } = a;
interface A extends Base<IsPrim<A>> { x: number }
const sentinel: number = "";
