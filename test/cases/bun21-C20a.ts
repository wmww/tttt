// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(c: boolean): number {
  let y: 1 | 2 = 1;
  while (true) {
    switch (y) { case 1: return 1; case 2: return 2; }
    y = 2;
    if (c) break;
  }
}
const sentinel: number = "";
