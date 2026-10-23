// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "types": [], "lib": ["es2022"], "outDir": "out", "noEmit": true }, "exclude": [null] }
// @filename: a.ts
const sentinel: number = "";
// @filename: out/o.ts
const o: number = "";
