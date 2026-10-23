// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const mi: Map<string, number> & { x: 1 };
mi["a"];
const sentinel: number = "";
