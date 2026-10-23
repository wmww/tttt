// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const n: number;
switch (n) { case 1: function c1(): void; function c2() {} }
const sentinel: number = "";
