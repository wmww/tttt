// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const l1 = { m() { return this.z; }, f() { return this.z; }, g: () => ({ a: 1 + {} }) };
const l2 = { m() { return this.z; }, f() { return this.z; }, g: () => ({ a: { b: 2 + {} } }) };
const l3 = { m() { return this.z; }, f() { return this.z; }, g: () => [{ a: 3 + {} }] };
const l4 = { m() { return this.z; }, f() { return this.z; }, g: () => ({ ["a"]: 4 + {} }) };
const l5 = { m() { return this.z; }, f() { return this.z; }, g: () => ({ a: 5 + {}, ...{} }) };
const l8 = { m() { return this.z; }, f() { return this.z; }, g: () => ({ a: [8 + {}] }) };
const sentinel: number = "";
