// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class B { static "#x" = 1; }
class D extends B { static "#x" = ""; }
const sentinel: number = "";
