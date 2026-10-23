// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface MJ { readonly a: number }
class MJ { readonly a: number; constructor() { this.a = 1; } }
const sentinel: number = "";
