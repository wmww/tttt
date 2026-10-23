// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U<T, V extends { k: T }> = (V & { k: "b"; y: 2 }) | { k: "c"; z: 3 };
type K<T, V extends { k: T }> = keyof U<T, V>;
function f<T, V extends { k: T }>() { const n: { q: K<T, V> } = 1; }
const sentinel: number = "";
