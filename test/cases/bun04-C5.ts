// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export type S4 = ({ a: b }: any, c = b) => void;
const sentinel: number = "";
