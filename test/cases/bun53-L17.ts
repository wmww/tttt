// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "allowJs": true, "checkJs": true } }
// @filename: a.js
const dec = 0, dec2 = 0;
@dec export @dec2 function f1() {}
@dec export @dec2 const x2 = 1;
@dec export @dec2 async function f3() {}
@dec export @dec2 class C4 {}
