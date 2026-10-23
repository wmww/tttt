// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
declare const dec: any;
interface I { m(@dec x: any): void }
const sentinel: number = "";
