// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const x: any; declare const async: any; type U = 1;
async <T extends U>(x);
async <T = U>(x);
async <const T>(x);
async <T extends U>(x: any);
