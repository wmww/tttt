// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
export {};
const sentinel: number = "";
// @filename: b.d.ts
export default function* (): any;
