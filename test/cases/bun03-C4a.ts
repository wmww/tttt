// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const f: (opts?: { a: number }) => void = ({}) => {};
const g: (x: number) => void = ([]) => {};
const sentinel: number = "";
