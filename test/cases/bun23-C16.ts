// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const cs = "k";
const o3 = { [cs]: 1, b: 2, [cs]: 3 }; const y3: 1 = o3;
const sentinel: number = "";
