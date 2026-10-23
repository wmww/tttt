// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const K: string = compute({ a: "" });
function compute(o: Record<string, string>) { return o[K]; }
const K3: string = compute3({ a: "" });
function compute3(o: Record<string, string>) { return o[K3].length + ""; }
// an access on the way is compared with it: TS7023 in both
const K2: string = compute2({ a: "" });
function compute2(o: Record<string, string>) { o.x = ""; return o[K2]; }
const sentinel: number = "";
