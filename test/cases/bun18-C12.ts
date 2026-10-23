// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1<T extends { a: string } | { a: boolean }>() {
  const y: T | { a: 1 } = { a: 2 };
}
const sentinel: number = "";
