// @noEmit: true

// `export =` of an `import type` alias of this file whose target is only a type: TS1282, not also TS1291 (tsc's
// isolatedModules check excludes a type-only declaration in the same file, whatever it names)
// @verbatimModuleSyntax: true
// @target: esnext
// @module: commonjs
// @moduleResolution: bundler
// @Filename: /a.ts
interface I {}
export = I;

// @Filename: /b.ts
import I = require("./a");

// @Filename: /c.ts
interface I {}
namespace I {
    export const x = 1;
}
export = I;

// @Filename: /d.ts
import I = require("./c");
import type J = require("./c");
export = J;

// @Filename: /e.d.ts
interface I {}
export = I;

// @Filename: /f.ts
import type I = require("./e");
export = I;

// @Filename: /z.ts
// test harness is weird if the last file includs a require >:(