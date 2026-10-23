// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.disposable"], "target": "es2022", "module": "nodenext", "moduleDetection": "legacy"}}
// @filename: a.ts
export {};
const sentinel: number = "";
// @filename: b.ts
await using a = null;
// @filename: package.json
{ "type": "module" }
