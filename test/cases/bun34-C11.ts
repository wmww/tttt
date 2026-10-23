// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class B { p: number }
declare class D extends B { declare p; }
const sentinel: number = "";
