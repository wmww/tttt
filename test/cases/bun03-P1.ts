// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function mk(): symbol;
class K { static readonly k = mk(); }
class B {}
class D extends B { override [K.k]() {} }
const sentinel: number = "";
