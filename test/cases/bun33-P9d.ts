// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = import("./m").<1>;
const sentinel: number = "";
// @filename: m.ts
export {};
