// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class K<D> { d: D; e(): K<[D]>; x(): string }
declare class S<D> { d: D; e(): S<[D]>; x(): D extends [[any]] ? number : string }
declare class R extends K<number> {}
declare function mk<X>(): S<X>
const k: K<number> = mk<number>()
const sentinel: number = "";
