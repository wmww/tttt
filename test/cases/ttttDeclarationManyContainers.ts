// Every module of the program that exports a symbol is a candidate container (tsgo's getExternalModuleContainers has
// no limit, tttt kept 8): the one with the fewest path components wins even when many others come first, crawlee's
// `import("crawlee").CrawleeLogger` among 13 re-exporting packages
// @declaration: true
// @module: esnext
// @moduleResolution: bundler
// @filename: /src/lib/deep/types.ts
export interface Logger { log(message: string): void }
export declare function make(): Logger;
// @filename: /src/lib/deep/r1.ts
export * from "./types";
// @filename: /src/lib/deep/r2.ts
export * from "./types";
// @filename: /src/lib/deep/r3.ts
export * from "./types";
// @filename: /src/lib/deep/r4.ts
export * from "./types";
// @filename: /src/lib/deep/r5.ts
export * from "./types";
// @filename: /src/lib/deep/r6.ts
export * from "./types";
// @filename: /src/lib/deep/r7.ts
export * from "./types";
// @filename: /src/lib/deep/r8.ts
export * from "./types";
// @filename: /src/lib/deep/r9.ts
export * from "./types";
// @filename: /src/lib/deep/r10.ts
export * from "./types";
// @filename: /src/index.ts
export * from "./lib/deep/types";
// @filename: /src/main/x.ts
import { make } from "../lib/deep/types";
export const logger = make();
