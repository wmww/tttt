// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const c: boolean;
const vs: string[] = ["a", ...(c ? [1] : []), "b"];
const sentinel: number = "";
