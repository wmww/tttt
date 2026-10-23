// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const ks: string;
const e: 1 = { [ks]?: 1 };
const sentinel: number = "";
