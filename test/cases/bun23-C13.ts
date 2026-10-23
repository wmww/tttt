// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const v1: 1 = ({ 0x10: b }: any) => {};
const sentinel: number = "";
