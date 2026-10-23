// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const sentinel: number = "";
// @filename: m.d.ts
declare var y: any;
declare enum E { A = delete y }
