// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a = { m() { return this.z; }, f() { return this.z; }, g() { return (1 + {}); } };
const b = { m() { return this.z; }, f() { return this.z; }, g: () => { return ([2 + {}]); } };
const c = { m() { return this.z; }, f() { return this.z; }, get g() { return (3 + {}, 1); } };
const d = { m() { return this.z; }, f() { return this.z; }, g: function () { return ((4 + {}) as any); } };
// without the parentheses both tools lose the error
const e = { m() { return this.z; }, f() { return this.z; }, g() { return 5 + {}; } };
const sentinel: number = "";
