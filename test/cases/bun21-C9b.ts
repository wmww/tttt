// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const sy: unique symbol;
function g({ [sy]: k, v }: { [sy]: "a"; v: string } | { [sy]: "b"; v: number }) { if (k === "a") v.length; }
const sentinel: number = "";
