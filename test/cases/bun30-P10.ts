// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var x: any;
class K { @(x) [k: string]: any }
const sentinel: number = "";
