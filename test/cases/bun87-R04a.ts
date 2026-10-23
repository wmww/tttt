// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function sp0<T>(t: T) { return { ...{} }; }
function rs0<T>(t: T) { const { a, ...r } = { a: 1 }; return r; }
function un(u: unknown) {
  const a: ReturnType<typeof sp0<3>> | null | undefined = u;
  const b: ReturnType<typeof rs0<3>> | null | undefined = u;
}
const sentinel: number = "";
