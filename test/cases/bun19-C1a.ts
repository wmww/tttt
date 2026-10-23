// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T>(x: number) { const y: T extends string ? number : number = x; }
const sentinel: number = "";
