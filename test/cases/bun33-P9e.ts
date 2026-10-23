// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = import("./m").#a;
const sentinel: number = "";
// @filename: m.ts
export {};
