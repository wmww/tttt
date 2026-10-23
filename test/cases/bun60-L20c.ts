// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: a.js
const k = "foo";
class D3 { static [k] = 1; static { this.foo = 2; } }
class D4 { static [k] = 1; static m() { this.foo = 2; } }
class D5 { [k] = 1; constructor() { this.foo = 2; } }
class D6 { [k] = 1; m() { this.foo = 2; } }
class D7 { static foo = 1; static { this.foo = 2; } }
class D8 { static [k]; static { this.foo = 2; } }
class D9 { static { this.foo = 2; } static [k] = 1; }
/** @type {number} */
const sentinel = "";
