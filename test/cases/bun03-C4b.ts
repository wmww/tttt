// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cbg<T>(v: T, f: (x: T) => void): void;
cbg(1, ([]) => {});
const sentinel: number = "";
