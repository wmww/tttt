// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.js
Object.defineProperty(exports, "g1", { get() { return exports.g1; } });
Object.defineProperty(exports, "g2", { get: () => exports.g2 });
Object.defineProperty(exports, "g3", { get: () => { return exports.g3; } });
Object.defineProperty(exports, "g4", { get: function () { return exports.g4; } });
Object.defineProperty(exports, "g5", { get: function named() { return exports.g5; } });
Object.defineProperty(exports, "g6", { get() { return exports.g7; } });
Object.defineProperty(exports, "g7", { get() { return exports.g6; } });
Object.defineProperty(exports, "v1", { value: () => exports.v1 });
Object.defineProperty(exports, "v2", { value() { return exports.v2; } });
Object.defineProperty(module.exports, "g8", { get() { return module.exports.g8; } });
function F() {}
Object.defineProperty(F, "g9", { get() { return F.g9; } });
Object.defineProperty(F.prototype, "g10", { get() { return new F().g10; } });
/** @type {number} */
const sentinel = "";
