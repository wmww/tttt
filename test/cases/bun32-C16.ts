// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class B { x: number; }
declare class D extends B { declare x; }
const sentinel: number = "";
