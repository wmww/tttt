// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowUnreachableCode": false } }
// @filename: a.ts
function f(c: boolean): number {
  let y: 1 | 2 = 1;
  while (c) {
    switch (y) { case 1: return 1; case 2: return 2; }
    y = 2;
  }
  return 0;
}
const sentinel: number = "";
