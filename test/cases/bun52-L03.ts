// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(a = nope): void;
let g: (x: number) => void;
g = f;
const sentinel: number = "";
