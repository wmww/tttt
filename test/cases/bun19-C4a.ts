// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class Foo { a = 1; p!: this extends Foo ? 1 : 2; m() { const x: 1 = this.p; } }
const sentinel: number = "";
