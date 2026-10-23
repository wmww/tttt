// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function pass2<F>(f: F): F;
const a2 = pass2(function* () { yield 1; });
const sentinel: number = "";
