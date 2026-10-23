// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f({ kind, v }: { kind: "a"; v: string } | { kind: "b"; v: number }) { var kind: "a" | "b"; if (kind === "a") v.length; }
const sentinel: number = "";
