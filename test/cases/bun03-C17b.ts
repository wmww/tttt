// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "erasableSyntaxOnly": true } }
// @filename: a.ts
declare export = 1;
const sentinel: number = "";
