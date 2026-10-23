// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
function missed() {
  let x: string | 1 | boolean = "";
  let y: string | number = "";
  while (c()) {
    x;
    if (y === 1) { }
    x = y;
    if (c()) y = 0;
  }
}
function otherText() {
  let x: string | boolean = "";
  let y: string | number = "";
  while (c()) {
    x;
    if (y === 1) { }
    x = y;
    if (c()) y = 0;
  }
}
function reportedTwice() {
  let x: string | number | boolean = "";
  let y: string | number = "";
  while (c()) {
    x;
    if (y === 1) { }
    x = (y satisfies never);
    if (c()) y = 0;
  }
}
const sentinel: number = "";
