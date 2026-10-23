// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
declare const f: (() => "a") | undefined;
const o = { [f()]: 1 };
const sentinel: number = "";
