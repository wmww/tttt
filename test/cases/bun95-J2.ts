// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"]}}
// @filename: a.d.ts
declare module "m" {
  class C {}
  namespace C {
    interface Options { a?: number }
  }
  export = C;
}
// @filename: b.d.ts
declare module "m" {
  interface Options { b?: number }
}
// @filename: c.ts
const sentinel: number = "";
