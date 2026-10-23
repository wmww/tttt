// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
function minimal() {
  let y: string | number = "";
  while (c()) {
    if (y === 1) { }
    switch (y) { case 0: y = 0; }
  }
}
function likely(a: string[]) {
  let v: string | number = "";
  for (const e of a) {
    if (v === 1) { c(); }
    switch (v) {
      case 0: v = 1; break;
      case "": v = 0; break;
    }
  }
}
const sentinel: number = "";
