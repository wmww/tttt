// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function next(e: E1) { return e + 1; }
enum E1 { A = 1, B = next(E1.A) }
declare function make(n: number): E2;
enum E2 { A = 1, B = make(1) }
declare const first: E3;
enum E3 { A = 1, B = first }
const sentinel: number = "";
