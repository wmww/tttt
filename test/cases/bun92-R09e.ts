// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Other { o: 1 } interface Base { b: 1 } interface Sub extends Base { }
type Cond<T> = T extends string | number ? { y: 1 } : { z: 1 };
declare const B: new () => Cond<A>;
class A extends B { x = 1 }
declare const a: A; a.x; a.y; a.z;
const sentinel: number = "";
