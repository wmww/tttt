// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class R {
  readonly x: number;
  constructor() { (this).x = 2; }
}
const sentinel: number = "";
