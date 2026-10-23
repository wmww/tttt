// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: d01.ts
function w() { var v = (a, ...) => 1; }
// @filename: d03.ts
var v = (a, ...);
// @filename: d07.ts
var v = <T>(a, ...) => 1;
// @filename: d08.ts
var v = (a, ...,) => 1;
// @filename: d09.ts
var v = (a, ... = 1) => 1;
// @filename: d10.ts
var v = (a, ...: any) => 1;
// @filename: d12.ts
var v = (a, ...1) => 1;
// @filename: d15.ts
var v = (a, ...) => { };
// @filename: d16.ts
f((a, ...) => 1);
// @filename: e061.ts
(a, ...) => 1;
// @filename: p061.ts
async function w(q = (a, ...) => 1) {}
// @filename: s061.ts
var v = (a, ...) => 1;
