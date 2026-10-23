// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = { [K in "a"]: B[K & keyof B] };
declare const a: A; a.a;
const sentinel: number = "";
// @filename: b.ts
type B = { [K in "a"]: A[K & keyof A] };
declare const b: B; b.a;
