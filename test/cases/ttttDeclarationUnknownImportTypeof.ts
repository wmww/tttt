// @declaration: true
// @strict: true
// A reused `typeof X` whose X is an import of an unresolved module, printed in another file: tsc's serializeTypeName
// resolves it to unknownSymbol, which the file names by its first import that resolves to nothing too, painting it,
// as it does a type reference's name (ttttDeclarationUnknownImportRenamed): better-auth's `typeof BASE_ERROR_CODES`
// printed as `typeof BetterAuthClientOptions` in another file
// @filename: a.ts
import type { Opts } from "missing-core";
import type { CODES } from "missing-core/error";
export function f<O extends Opts>() { return {} as { e: Partial<O> & typeof CODES } }
// @filename: b.ts
import type { Opts } from "missing-core";
import { f } from "./a";
export async function g<C extends Opts>() { const r = f<C>(); return { ...r }; }
// @filename: c.ts
import { f } from "./a";
export async function h() { const r = f(); return { ...r }; }
