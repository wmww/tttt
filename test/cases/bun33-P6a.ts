// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
function* g() { let x: { [yield 1]: 1 }; }
const sentinel: number = "";
