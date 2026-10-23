// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let o: object, e: {};
[o, e, {}].push(1);
const sentinel: number = "";
