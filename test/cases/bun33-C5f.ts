// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface G<T> {}
interface X extends G<number>.x {}
const sentinel: number = "";
