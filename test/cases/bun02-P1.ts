// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const n: number;
switch (n) { case 1: declare function c(): void; case 2: function c() {} }
const sentinel: number = "";
