// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: j.js
class B { constructor() { this.x = new B().x; } m() { return this.x; } }
class D { constructor() { this.x = mk().x; } m() { return this.x; } } /** @returns {D} */ function mk() { return new D(); }
class E { constructor() { const t = mk2().x; this.x = t; } m() { return this.x; } } /** @returns {E} */ function mk2() { return new E(); }
class K { constructor() { this.x = new K; this.y = new K().x.y; } m() { return [this.x, this.y]; } }
class O { constructor(/** @type {O} */ o) { this.x = o.x; } m() { return this.x; } }
/** @type {number} */ const sentinel = "";
