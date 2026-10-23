// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface M3<T extends U, U extends T> { a: T } interface M3<T extends U, U extends T> { b: U }
interface M6<T extends T> { a: T } interface M6<T extends T> { b: T }
class M9<T extends T> { a!: T } interface M9<T extends T> { b: T }
interface M11<T extends M11<T>["a"]> { a: T } interface M11<T extends M11<T>["a"]> { b: T }
const sentinel: number = "";
