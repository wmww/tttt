// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Cn<T, U> = T extends string ? [T, U] : Cn<U | "x", "y">;
declare let cn: Cn<1, "z">;
const okcn: ["x", "y"] | ["y", "y"] = cn;
const sentinel: number = "";
