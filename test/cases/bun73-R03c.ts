// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
function f() {
  let x = []; const y = [];
  while (c()) {
    do {
      if (x.length) { for (y.push(0); (x = []); ) { } }
      else { if (x === y) return; }
    } while (x.push(1));
  }
}
const sentinel: number = "";
