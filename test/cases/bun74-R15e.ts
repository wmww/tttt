// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const x: any; type T = any;
<T>(x?: number);
// @filename: b.ts
declare const x2: any, y2: any; type T2 = any;
<T2>(x2? = 1);
