// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<X>() { return class<T = X> { y!: T } }
class E extends mk<string>() { m() { return this.y.length } }
const sentinel: number = "";
