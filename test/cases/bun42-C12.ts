// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<X extends string>(x: `${X}`): X;
const r: 0 = f(`${"\ud83d" + "\ude00"}`);
const sentinel: number = "";
