// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class B4<T> { b!: T }
class A4 extends B4<typeof a4> { own = 1 }
const a4 = new A4().own;
const sentinel: number = "";
