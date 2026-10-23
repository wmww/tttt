// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a0 = 1, a1 = a0, a2 = a1, a3 = a2, a4 = a3, a5 = a4, a6 = a5, a7 = a6, a8 = a7, a9 = a8,
  a10 = a9, a11 = a10, a12 = a11, a13 = a12, a14 = a13, a15 = a14, a16 = a15, a17 = a16;
enum E { B = a17 }
const y: E = 2;
const sentinel: number = "";
