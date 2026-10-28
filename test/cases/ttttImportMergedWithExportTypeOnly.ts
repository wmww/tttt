// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "verbatimModuleSyntax": true } }
// @filename: m.ts
class K {}
export type { K };
class L {}
export type { L };
// @filename: a.ts
// An import merged with an exported declaration of its name: tsc's local symbol's ExportSymbol is the exported
// declaration, which checkAliasSymbol checks in the alias's place, so nothing makes the import type-only (no TS1485;
// bun 04-C12). Without the export it is
import { K } from "./m";
export namespace K {}
import { L } from "./m";
namespace L {}
