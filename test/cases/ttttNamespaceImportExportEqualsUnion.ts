// @strict: true
// @module: nodenext
// @noTypesAndSymbols: true
// A namespace import of a module whose `export =` is a union is `{}`: tsc's cloneTypeAsModuleType takes the members
// resolveStructuredTypeMembers gives the (spread with a synthetic default) union, and a union has none; a union's
// `default` property counts for wanting the clone (bun's 08-C6a, 08-C6b)
// @filename: u1.d.cts
declare const u: { a: number } | { a: string; b: number }; export = u;
// @filename: u3.d.cts
declare const u: { default: number; a: 1 } | { default: string; a: 2 }; export = u;
// @filename: a.mts
import * as u1 from "./u1.cjs"; u1.a; u1.default.a;
// @filename: b.cts
import * as u3 from "./u3.cjs"; u3.a; u3.default;
