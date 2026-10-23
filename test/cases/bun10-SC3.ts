// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: j.js
/** @type {{ new(): {}, a: string[] }} */
const CE2 = class {};
CE2.a = [];
function F() {}
/** @type {{ (): void, b: string[] }} */
F.a = function () {};
F.a.b = [];
/** @type {number} */
const sentinel = "";
