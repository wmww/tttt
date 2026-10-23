// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const oz: { z: 1 };
function f<T extends { a: number }>() { const x1: Partial<T> = oz; const x2: Partial<T> = 1; }
const sentinel: number = "";
