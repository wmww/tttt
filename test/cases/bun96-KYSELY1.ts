// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class K<D> {
  d: D
  p(): K<D>
  e<T>(): K<[D, T]>
  x(): string
}
declare class R<D> extends K<D> {
  p(): R<D>
  e<T>(): R<[D, T]>
  x(): unknown
}
const sentinel: number = "";
