// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let f: { (x: { "é-x": 1 }): void };
f = {};
const sentinel: number = "";
