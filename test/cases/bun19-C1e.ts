// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g<T>(a: number, x: number, y: T extends string ? typeof a : number = x) {}
const sentinel: number = "";
