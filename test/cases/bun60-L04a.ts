// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
function f() { const s = Symbol(); return { [s]: 1 }; }
export class C0 { constructor() { this.x = f(); } }
export class C1 { [+1]() {} constructor() { this[1] = f(); } }
export class C2 { 1() {} constructor() { this[1] = f(); } }
export class C3 { [1]() {} constructor() { this[1] = f(); } }
export class C4 { [-1]() {} constructor() { this["-1"] = f(); } }
export class C5 { [+1]() {} constructor() { this["+1"] = f(); } }
/** @type {number} */
const sentinel = "";
