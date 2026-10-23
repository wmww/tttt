// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2017", "importHelpers": true, "module": "esnext" } }
// @filename: a.ts
export {};
type F = ({ ...rest }: { x: number }) => void;
const sentinel: number = "";
