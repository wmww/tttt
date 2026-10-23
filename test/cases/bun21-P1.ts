// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function make<T>(): T;
f();
function f() { let x = []; x[make()] = "a"; return x; }
const sentinel: number = "";
