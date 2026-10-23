// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const mt: [number, number] | readonly [number, number];
mt[0] = 1;
const sentinel: number = "";
