// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { a?: { b?: string } } | undefined;
if (o?.a!?.b) o.a;
const sentinel: number = "";
