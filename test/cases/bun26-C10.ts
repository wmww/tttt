// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function r<T>({ a, ...rest }: { a: 1; b: 2 }) { return rest; }
const n: never = null! as ReturnType<typeof r<1>> | ReturnType<typeof r<2>>;
const sentinel: number = "";
