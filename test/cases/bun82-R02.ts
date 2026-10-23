// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function f1() {
  let mode: "r" | "w" = "r";
  while (cond()) {
    const [m, n] = [mode, 1];
    switch (m) {
      case "r":
        mode = "w";
        break;
    }
  }
}
function f2() {
  let mode: "r" | "w" = "r";
  while (cond()) {
    const { m } = { m: mode };
    switch (m) {
      case "r":
        mode = "w";
    }
    const t: "r" | "w" = m;
  }
}
const sentinel: number = "";
