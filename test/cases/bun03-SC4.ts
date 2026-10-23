// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2017", "importHelpers": true } }
// @filename: a.ts
export type F = ({ a, ...r }: { a: number; b: number }) => void;
const sentinel: number = "";
