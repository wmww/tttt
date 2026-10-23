// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "exactOptionalPropertyTypes": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let x1: { a?: number } & { a: number | undefined };
x1.a = undefined;
const sentinel: number = "";
