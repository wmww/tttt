// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K<T> { constructor(public x: T) {} }
function mk<X>() { return class D<T extends (a: X) => void> extends K<T> {} }
new (mk<string>())(a => a.length);
const sentinel: number = "";
