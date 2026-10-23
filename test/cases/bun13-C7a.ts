// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U<T> = ({ k: T | "a"; x: 1 } & { k: "b"; y: 2 }) | { k: "c"; z: 3 };
type K<T> = keyof U<T>;
const r1: K<"z"> = "z";
const sentinel: number = "";
