// tsgo's getAlternativeContainingModules finds the enclosing file's imports again with resolveExternalModuleName from
// the enclosing declaration, in the file's default resolution mode: under `module: esnext` (bundler resolution) that
// is none while the imports were resolved as ESM, so no import is found and every module of the program that exports
// the symbol is a candidate container, the one with the fewest path components first (better-auth's
// `import("../..").Statements` where tttt named the imported `../../access`)
// @declaration: true
// @module: esnext
// @moduleResolution: bundler
// @filename: /src/access/types.ts
export type Statements = { readonly [resource: string]: readonly string[] };
// @filename: /src/access/index.ts
export * from "./types";
import type { Statements } from "./types";
export declare function createAccessControl<const T extends Statements>(s: T): {
    newRole<const R extends Statements>(statements: R): R;
    statements: T;
};
// @filename: /src/index.ts
export * from "./access";
// @filename: /src/plugins/admin/statement.ts
import { createAccessControl } from "../../access";
export const ac = createAccessControl({ user: ["create", "list"] } as const);
