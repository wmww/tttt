// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M1 = { [K in never as "a"]: 1 };
declare let m1: M1;
const m1b: string = m1.a;
const sentinel: number = "";
