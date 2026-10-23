// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const dec: any;
const f = (a: any, @dec x: any) => 1;
const g = <T,>(@dec x: T) => 1;
const h = (a: any, @dec() x?: any, @dec ...y: any[]) => 1;
const i = async (a: any, @dec x: any) => 1;
const j = (a = 1, @dec { b }: any) => 1;
const sentinel: number = "";
