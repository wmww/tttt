// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
var x = function() {} = 1;
(function() {} = 1);
const sentinel: number = "";
