// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true

// A descriptor passed as a variable has `writable: boolean` once widened, but the property assignment's initializer
// still says false: the property defined is read-only (tsc's isReadonlyAssignmentDeclaration checks it)
// @filename: mod1.js
const ro = { value: 42, writable: false };
Object.defineProperty(exports, "thing", ro);
const rw = { value: 42, writable: true };
Object.defineProperty(exports, "other", rw);
// @filename: importer.js
const mod = require("./mod1");
mod.thing = 0;
mod.other = 0;
