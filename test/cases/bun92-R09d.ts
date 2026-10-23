// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type IsPrim<T> = T extends string | number ? true : false;
interface Base<F> { f: F }
type X = IsPrim<A>;
interface A extends Base<X> { x: number }
const sentinel: number = "";
