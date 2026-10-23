// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C {
  static x: string | undefined;
  constructor() {
    if (new.other.x) { new.target.x.length; }
    if (new.target.x) { new.target.x.length; }
  }
}
const sentinel: number = "";
