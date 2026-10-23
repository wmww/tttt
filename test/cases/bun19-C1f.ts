// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { p!: this extends string ? this : this; m() { this.p = this; } }
const sentinel: number = "";
