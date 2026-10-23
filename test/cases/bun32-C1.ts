// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class R { readonly x: number = 0; constructor() { (this).x = 1; } }
const sentinel: number = "";
