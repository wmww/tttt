// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g<T extends { (): void; id: (x: number) => void }>() {
  const F: T = () => {};
  F.id = x => {};
  F["id"] = x => {};
}
const sentinel: number = "";
