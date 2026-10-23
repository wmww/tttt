// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: unique symbol;
const c = "x";
export class C {
  ["\0\0\u007f\u007f\u007f\u007f"]: number;
  ["\0\u0001\u007f\u007f\u007f\u007f"]: number;
  [c]: number;
  [k]: number;
  constructor() {
    for (;;) {
      if (Math.random()) break;
    }
    this[c] = 1;
  }
}
const sentinel: number = "";
