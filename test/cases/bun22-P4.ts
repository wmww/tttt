// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K<T> { constructor(cb: T) {} }
class D<T> extends K<T> {}
declare const s: typeof D<(y: 1) => void>;
const t: typeof D<(y: 2) => void> = s;
const sentinel: number = "";
