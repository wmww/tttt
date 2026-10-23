// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class S { set c(this: string) {} } new S().c = 1;
const sentinel: number = "";
