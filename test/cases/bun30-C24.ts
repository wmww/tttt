// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export class A { #a = 1; m() { const { #a: x } = this; } }
const sentinel: number = "";
