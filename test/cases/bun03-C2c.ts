// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
class B { constructor() { this.asg = 1; } }
class D extends B { get asg() { return 1 } }
/** @type {number} */
const sentinel = "";
