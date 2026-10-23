// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k: number = null! as keyof { 1e400: 1 };
const sentinel: number = "";
