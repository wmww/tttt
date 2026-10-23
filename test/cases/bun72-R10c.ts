// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Cond<T> = T extends string | number ? { y: 1 } : { z: 1 };
class Base<T> { b!: T }
declare const pre: C1; pre.x;
class C1 extends Base<Cond<C1>> { x = 1 }
function early(c: C2) { return c.x; }
class C2 extends Base<Cond<C2>> { x = 1 }
let t3: Cond<C3>;
class C3 extends Base<Cond<C3>> { x = 1 }
let t4: C4 | undefined;
class C4 extends Base<Cond<C4>> { x = 1 }
let t5 = (c: C5): string | number => c;
class C5 extends Base<Cond<C5>> { x = 1 }
const sentinel: number = "";
