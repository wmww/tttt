// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "isolatedModules": true } }
// @filename: a.ts
export {};
declare const enum CE { B = "b" }
type F = ({ [CE.B]: [] }: any) => void;
const sentinel: number = "";
