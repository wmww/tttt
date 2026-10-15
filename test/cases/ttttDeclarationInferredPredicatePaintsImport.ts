// The type of an inferred type predicate is printed by the node builder, which tracks the names in it: an import it
// names through is painted and kept (vscode's extpath.d.ts dropped `import { CharCode }` for `code is CharCode`)
// @declaration: true
// @filename: /charCode.ts
export const enum CharCode { Slash = 47, Backslash = 92 }
// @filename: /extpath.ts
import { CharCode } from "./charCode";
export function isPathSeparator(code: number) {
    return code === CharCode.Slash || code === CharCode.Backslash;
}
