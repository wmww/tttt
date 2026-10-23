// @strict: true
// @resolveJsonModule: true
// @module: esnext
// @moduleResolution: bundler
// @filename: e.json
// @filename: a.ts
// An empty JSON file is tsc's emptyObjectType, which has no symbol and so no inferable index
import j from "./e.json";
const m1: Record<string, unknown> = j;
const { a, ...rest } = { a: 1 };
const m6: Record<string, unknown> = rest;
