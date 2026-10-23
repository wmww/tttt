// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const ar = () => {}; ar['d-e'] = 3; ar['1'] = 1;
const t2: string = ar;
const sentinel: number = "";
