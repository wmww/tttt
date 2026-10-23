// origin: sourcesweep foldkit https://github.com/foldkit/foldkit packages/foldkit/tsconfig.build.json
// flip: yes
// An import of a namespace named like the global generic alias Record (effect's `export * as Record`) and written only
// as `Record<K, V>` in types: tsgo's getTypeFromTypeAliasReference also looks the name up as an alias (for the alias it
// would print the instantiation by), which marks the import referenced: no TS6133. Without type arguments, or as the
// body of a type alias, there's no such lookup and the import is unused
// @strict: true
// @noUnusedLocals: true
// @filename: rec.ts
export const map = 1;
// @filename: lib.ts
export * as Record from "./rec";
// @filename: a.ts
import { Record } from "./lib";
export const t1: Readonly<Record<string, number>> = {};
// @filename: c.ts
import { Record } from "./lib";
export const t3 = 1;
// @filename: e.ts
import { Record } from "./lib";
type X = Record<string, number>;
export const t5: X = {};
