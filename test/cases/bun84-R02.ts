// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = ([...a,]: any);
// @filename: b.ts
type B = ({...a,}: any, b: string);
// @filename: c.ts
type C = string | ([...a,]: any) => void;
// @filename: e.ts
type D = ([...a,]?);
