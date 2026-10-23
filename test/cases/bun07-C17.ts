// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare let n: number;
for (({ n }) in {}) {}
for (([n]) in {}) {}
const sentinel: number = "";
