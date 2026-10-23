// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F = () => void;
declare const o: { [k: string]: F };
if (o.foo) { o.bar(); }
o.foo && o.bar();
const sentinel: number = "";
