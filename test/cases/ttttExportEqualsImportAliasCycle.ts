// @strict: true
// @module: commonjs
// @noEmit: true

// An import of a module with `export =` resolves through the type of what it assigns (tsc's getExternalModuleMember,
// getTargetOfModuleDefault, resolveESModuleSymbol): when that type needs the import itself, resolveAlias is
// re-entered, TS2303 at the import, and the alias is the error type from then on. These cycles depend on check
// order: one project per shape (bun's differential cases 08-C11a)
// @filename: a.ts
import d from "./a"; const v = { n: 1, m: d }; export = v;
const sentinel: number = "";
