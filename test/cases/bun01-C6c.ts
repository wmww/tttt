// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export {};
global { interface FromUndeclared { a: number } }
declare global { interface FromDeclared { a: number } }
const sentinel: number = "";
// @filename: b.ts
export {};
let y: FromDeclared;
