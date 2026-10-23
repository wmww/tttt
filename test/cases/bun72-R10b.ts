// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Cond<T> = T extends string | number ? { y: 1 } : { z: 1 };
declare const ctor: new () => Cond<C7>;
class C7 extends ctor { x = 1 }
new C7().z;
const sentinel: number = "";
