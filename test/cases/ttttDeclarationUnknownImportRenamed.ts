// @declaration: true
// @strict: true
// A reused annotation naming an import of an unresolved module, printed in another file: tsc's
// trackExistingEntityName doesn't find the name there, serializeTypeName resolves it to unknownSymbol, which the
// file names by its first import that resolves to nothing too (getAccessibleSymbolChain, painting it): `Plugin<Plugin>`
// (better-auth's test-utils suites, shadcn's templates). Without such an import the node is reused as written.
// @filename: a.ts
import type { Opts } from "missing-mod";
import type { Adapter } from "missing-mod/adapter";
export const make = (fn: (h: { adapter: () => Promise<Adapter<Opts>>; get: () => Opts }) => void) => fn;
// @filename: b.ts
import type { Plugin } from "missing-mod";
import { make } from "./a";
export const suite = make((h) => {});
export const g = (x: Parameters<Parameters<typeof make>[0]>[0]) => x;
// @filename: c.ts
import { make } from "./a";
export const suite2 = make((h) => {});
