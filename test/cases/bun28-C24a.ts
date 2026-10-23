// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
const o = { get a() { return 1 }, get a() { return 1 }, public b: 1 };
const sentinel: number = "";
