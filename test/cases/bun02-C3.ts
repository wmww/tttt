// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function r1<T>(...a: T extends string ? number : string) {}
const sentinel: number = "";
