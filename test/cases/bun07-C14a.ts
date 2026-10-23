// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const a1 = { f: (this) => 1 };
const sentinel: number = "";
