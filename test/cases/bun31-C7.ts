// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const sentinel: number = "";
// @filename: b.d.ts
declare var arguments: any;
