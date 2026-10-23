// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface J { a(): void }
interface J { a?: 1 }
declare const x: J;
x.a();
const sentinel: number = "";
