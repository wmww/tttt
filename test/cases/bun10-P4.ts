// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { get x(): number { return 1; } set x(v: typeof w) {} }
declare const c: C;
const w = (c.x = 2);
const sentinel: number = "";
