// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U5 = { kind: any; a: 1 } | { kind: 5; b: 2 };
function t5(x: U5) { if (typeof x.kind === "string") { x.a; } }
const sentinel: number = "";
