// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface N { parent?: N }
declare const helper: { parent(n: N): N | undefined };
function w2(root: N) { let n: N | undefined = root; while (n) { const p = helper?.parent(n); n = p; } }
const sentinel: number = "";
