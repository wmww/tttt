// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const H: string = () => {};
H.charAt = x => "";
const sentinel: number = "";
