// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Boolean { 0: string }
const b: boolean = [1];
const sentinel: number = "";
