// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
export class U</*<*/ A, B> {}
const sentinel: number = "";
