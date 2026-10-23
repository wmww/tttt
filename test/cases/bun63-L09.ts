// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true }, "files": ["a.js", "b.ts"] }
// @filename: a.js
class C {
  constructor() {
    /** @type {ReturnType<typeof mk>} */
    this.x = 1;
  }
  x = mk();
}
function mk() { return new C().x; }
class D {
  constructor() {
    this.y = mk2();
  }
  /** @type {ReturnType<typeof mk2>} */
  y;
}
function mk2() { return new D().y; }
// @filename: b.ts
new C().x<number>();
new D().y<number>();
const sentinel: number = "";
