// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16", "moduleDetection": "legacy" } }
// @filename: a.mts
await 1;
const sentinel: number = "";
