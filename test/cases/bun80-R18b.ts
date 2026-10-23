// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs", "noUnusedLocals": true } }
// @filename: a.ts
const A = { b: 1 };
export namespace W {
  interface A { b: 1 }
  export import x = A;
}
export const sentinel: number = "";
// @filename: b.ts
class A { static b = 1 }
export namespace W {
  type A = { b: 1 };
  import x = A.b;
  x;
}
