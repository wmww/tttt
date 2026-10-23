// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { kind: "a"; a: number } | { kind: "b"; b: string };
function d1(x: U | undefined) { if ((x!).kind === "a") { x!.a; } }
const sentinel: number = "";
