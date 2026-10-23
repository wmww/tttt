// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F1 = (x: number) => void; type F2 = (y: string) => void;
function h1<T extends [F1, F2]>(t: T) { const [a = x => {}, b = y => {}] = t; }
const sentinel: number = "";
