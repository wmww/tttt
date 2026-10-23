// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { me: this }
declare function f<U>(x: { me: U }): U;
function g<T>(t: T & I): T & I { return f(t); }
const sentinel: number = "";
