// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Id extends Pick<typeof xd, "a"> {}
declare const yd: Id | undefined;
const xd = yd && 1;
const sentinel: number = "";
