// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function lo2(x: string): 1;
declare function lo2(x: ("a")): 2;
const lr2: 3 = lo2("a");
const sentinel: number = "";
