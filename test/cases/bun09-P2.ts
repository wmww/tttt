// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var v1: <T extends T>() => void;
declare var v1: <U>() => void;
const sentinel: number = "";
