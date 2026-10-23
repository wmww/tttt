// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let x: any;
({ 1n: x } = {});
[{ 1n: x }] = [{}];
for ({ 1n: x } of [{}]) {}
({ a: { 1n: x } } = { a: {} });
({ 1n: x = 3 } = {});
const sentinel: number = "";
