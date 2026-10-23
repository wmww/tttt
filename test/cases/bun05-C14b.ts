// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var Promise: PromiseConstructor;
new Promise(resolve => resolve());
const sentinel: number = "";
