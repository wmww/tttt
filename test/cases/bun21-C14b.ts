// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowUnreachableCode": false } }
// @filename: a.ts
const fail = (): never => { throw 0; };
function g() { fail(), 0; g; }
const sentinel: number = "";
