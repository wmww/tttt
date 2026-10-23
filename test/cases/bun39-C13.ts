// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true, "skipDefaultLibCheck": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.d.ts
declare let mydom: Nope2;
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","types":"index.d.ts"}
