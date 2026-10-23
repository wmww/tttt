// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type IsPrim<T> = T extends string | number ? true : false;
class Base<F> { f!: F }
class A extends Base<IsPrim<A>> { x = 1 }
let x: IsPrim<A>;
const sentinel: number = "";
