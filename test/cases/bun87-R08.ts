// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "esnext" } }
// @filename: a.ts
declare const o1: { a: 1 };
let a1: number;
({ 1n: a1 } = o1);
({ 1n: a1 = 2 } = o1);
[{ 0x1n: a1 }] = [o1];
for ({ 1n: a1 } of [o1]) {}
({ 1n: missing } = o1);
({ 1n: a1, ...o1 } = o1);
const sentinel: number = "";
