// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { x; constructor() { this.x = this; } }
new C().x = new C();
const sentinel: number = "";
