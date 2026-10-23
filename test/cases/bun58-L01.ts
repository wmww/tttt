// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C3 { get x(this) { return 1 } set x(v) { this.nope; } }
class C4 { get x() { this.nope; return 1 } set x(this, v) { this.nope2 } }
const o4 = { get x() { this.nope; return 1 }, set x(this, v) { this.nope2 } };
const sentinel: number = "";
