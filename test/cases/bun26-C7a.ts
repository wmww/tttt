// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type K<T> = keyof (({ k: T | "b" } & { k: "a" }) | { z: 3 });
const a: K<"c"> = "z";
const sentinel: number = "";
