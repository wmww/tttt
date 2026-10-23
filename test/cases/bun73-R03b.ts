// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
declare function s(): string;
function valid() {
  let x = []; let y = [];
  do {
    while (c()) {
      try { x.push(y); if ((x = [])) continue; } finally { const p: never = x; y.push(x.pop()); }
    }
    y = [];
  } while (c() && y.push(1));
}
function besideImplicitAny() {
  let x = []; let y = [];
  while (c()) {
    try { x.push(...y, {}); x.push(...y); const p: never = x; } catch { if (x === y || c()) break; }
    y.push(s());
  }
}
const sentinel: number = "";
