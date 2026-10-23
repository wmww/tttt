// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "nodenext", "moduleDetection": "legacy"}}
// @filename: a.ts
export {};
const sentinel: number = "";
// @filename: b.ts
for await (const x of []) {}
// @filename: package.json
{ "type": "module" }
