// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class B { constructor(cb: (x: number) => void) {} }
class D1 extends B { m() { super(x => x); } }
const sentinel: number = "";
