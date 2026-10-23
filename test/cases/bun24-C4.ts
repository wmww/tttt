// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Z = number;
type Y = [Iterable<Z, any, any>];
declare let y: Y; const v: 1 = y[0];
const sentinel: number = "";
