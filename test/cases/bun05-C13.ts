// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const p1 = (Symbol)(); const q1: number = p1;
const p2 = (Symbol).for("a"); const q2: number = p2;
const p3 = (Symbol.for)("a"); const q3: number = p3;
const sentinel: number = "";
