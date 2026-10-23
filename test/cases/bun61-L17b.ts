// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
function f() => 1
// @filename: b.js
function 1() {}
// @filename: c.js
class C { m() 1 }
