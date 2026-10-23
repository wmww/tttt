// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const module: { exports: () => void };
if (module.exports) {}
const sentinel: number = "";
