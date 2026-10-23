// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Args = [a: 1, b: number] | [a: 2, b: string];
const g: (...args: Args) => void = (kind, x?) => {
  throw 0;
  x.toString();
};
function outer<A extends Args>() {
  const h: (...args: A) => void = (kind, x) => {
    return;
    let y = x;
    y = true;
  };
}
const sentinel: number = "";
