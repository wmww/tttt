// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
class K { async n() {} }
new K().n();
const sentinel: number = "";
