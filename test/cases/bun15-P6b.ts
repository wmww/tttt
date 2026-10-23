// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const { a = () => 1 } = { a: () => 2 };
const sentinel: number = "";
