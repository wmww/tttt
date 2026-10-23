// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let a: number; declare let b: number;
({ a = 1 } += 1);
({ a: b = 1 } += 1);
[{ a = 1 }] += 1;
({ a = 1, ...{} } += 1);
const sentinel: number = "";
