// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(o: any, x: string | number) { with (o) { x = "a"; var g = () => x; } g().length; }
const sentinel: number = "";
