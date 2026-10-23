// @filename: m.ts
type T = number;
export type { T as as };
export const x = 1;
// @filename: a.ts
// An import or export specifier's leading `type` (and the `as` after it) is taken as an identifier name first (tsc's
// parseImportOrExportSpecifier via parseModuleExportName, createIdentifier's nextTokenWithoutCheck): spelled with an
// escape, it's no TS1260 even where it ends up the type-only modifier. Babel's
// type-only-import-export-specifiers/*-invalid-escaped-type-only fixtures
import { typ\u0065 as } from "./m";
import { typ\u0065 x } from "./m";
export { typ\u0065 as } from "./m";
