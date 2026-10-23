// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<U>(x: { me: U }): U;
class D { me = this; m(): this { return f(this); } }
const sentinel: number = "";
