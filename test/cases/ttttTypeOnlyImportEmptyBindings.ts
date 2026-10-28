// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "importHelpers": true, "esModuleInterop": true } }
// @filename: m.ts
export default class D {}
export const x = 1;
// @filename: a.ts
// `import type D, {}` has named bindings, empty ones: TS1363 (tsc's checkGrammarImportClause), which ends
// checkImportDeclaration's checks of the clause, its __importDefault helper (TS2354) too (bun 54-L01)
import type D1, {} from "./m";
import type D2, { x } from "./m";
export {};
