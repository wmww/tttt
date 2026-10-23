// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class I<T> { m!: T extends string ? 1 : 2; k<U>(a: [I<T>["m"] | I<U>["m"]]) { const n: never = a; } }
const sentinel: number = "";
