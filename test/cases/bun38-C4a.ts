// @filename: tsconfig.json
{ "extends": "./base.json", "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "" } }
// @filename: a.ts
export const l = [1].at(0);
export const sentinel: number = "";
// @filename: base.json
{ "compilerOptions": { "target": "es2015" } }
