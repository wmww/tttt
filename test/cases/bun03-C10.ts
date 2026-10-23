// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
abstract class G<T> { abstract v: T; }
declare const X: abstract new () => G<number> & G<string>;
class D extends X { get v(): never { throw 1 } }
class E extends X { v!: never }
const sentinel: number = "";
