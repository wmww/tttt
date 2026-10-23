// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: k01.ts
for (var i = (a = x in b) => 1;;) {}
// @filename: k02.ts
for (var i = <T>(a = x in b) => 1;;) {}
// @filename: k03.ts
for (var i = async (a = x in b) => 1;;) {}
// @filename: k04.ts
for (var i = (a: any = x in b) => 1;;) {}
// @filename: k07.ts
for (var i = [x in b];;) {}
// @filename: k11.ts
for (var i = (a) => { x in b };;) {}
// @filename: k14.ts
for (var i = class { m(a = x in b) {} };;) {}
// @filename: k15.ts
for (var i = { m(a = x in b) {} };;) {}
// @filename: k21.ts
for (var i = (a, c = x in b) => 1;;) {}
// @filename: k25.ts
for (i = (a = x in b) => 1;;) {}
// @filename: k31.ts
for (var i = function () { x in b };;) {}
// @filename: k32.ts
for (var i = class { m() { x in b } };;) {}
// @filename: k34.ts
for (var i = [...x in b];;) {}
// @filename: k35.ts
for (var i = class { static { x in b } };;) {}
// @filename: k36.ts
for (var i = function () { y = x in b };;) {}
// @filename: k38.ts
for (var i = function () { lbl: x in b };;) {}
