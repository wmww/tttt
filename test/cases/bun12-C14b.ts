// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g<T>() { const src = { a: 1 }; return { ...src }; }
type U = ReturnType<typeof g<1>> | ReturnType<typeof g<2>>;
const n: never = null! as U;
const sentinel: number = "";
