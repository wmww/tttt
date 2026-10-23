// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a1 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x; x = 1 + {}; return x; } };
const a2 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x: number | string = 0; x = 2 + {}; return x; } };
const a3 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x; [x] = [3 + {}]; return x; } };
const a4 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x; x = [4 + {}]; return x; } };
const a6 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x; x = { a: 6 + {} }; return x; } };
const a7 = { m() { return this.z; }, f() { return this.z; }, g: () => { let x; for (x of [7 + {}]) {} return x; } };
const sentinel: number = "";
