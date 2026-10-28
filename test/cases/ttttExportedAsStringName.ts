// TS2460 names the export as tsc's symbolToString does: a string literal name that isn't an identifier is quoted
// @target: esnext
// @module: esnext

// @filename: foo.ts
const foo = 0n;
export { foo as "0n" };

// @filename: correctUse.ts
import { "0n" as foo } from "./foo";
export { foo as "0n" };

// @filename: badImport.ts
import { 0n as foo } from "./foo";

// @filename: badImport2.ts
import { foo } from "./foo";

// @filename: badExport.ts
export { foo as 0n };

// @filename: badExport2.ts
export { 0n as foo };