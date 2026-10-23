// @module: commonjs
// @noEmit: true

// A UMD global caught in an alias cycle (its `export =` names nothing, and the spelling suggestion for that name
// resolves the global, in flight) has its TS2303 at the whole `export as namespace` statement
// @filename: foo.d.ts
export = Thing;
export as namespace Foo;
// @filename: a.ts
/// <reference path="foo.d.ts" />
let y = 1;
