// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const abcdefgh: unique symbol = Symbol();
class B { abcdefgh() {} }
class D extends B { override [abcdefgh]() {} }
const sentinel: number = "";
