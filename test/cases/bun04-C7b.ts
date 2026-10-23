// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
interface N { parent?: N }
declare function require(n: N): N | undefined;
function w3(root: N) { let n: N | undefined = root; while (n) { const p = require(n); n = p; } }
const sentinel: number = "";
