// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs", "noUnusedLocals": true } }
// @filename: a.ts
const A = { b: 1 };
export namespace W {
  namespace A {}
  export let t: A.b;
}
export const sentinel: number = "";
// @filename: b.ts
class A { static q = 1 }
export namespace W {
  namespace A {}
  export import x = A.b;
}
