// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
declare function log(v: unknown): void;
function likely(tokens: string[]) {
  let current: string | null = "";
  let result: string | number | null = null;
  let total = 0;
  for (const t of tokens) {
    if (result !== null) log(result);
    if (current) { log(current); }
    result = current ?? 0;
    if (t === ";") current = null;
    if (typeof result === "number") total += result.toFixed().length;
  }
}
function minimal() {
  let x: string | null = "";
  let y: string | number | null = null;
  while (c()) {
    y;
    if (x) { }
    y = x ?? 0;
    if (c()) x = null;
    const p: never = y;
  }
}
function nested() {
  let x: string | null = "";
  let y: string | number | null = null;
  while (y == null) {
    while (typeof x === "string" || c()) {
      y = x ?? 0;
    }
    if (c()) x = null;
    const p: never = y;
  }
}
const sentinel: number = "";
