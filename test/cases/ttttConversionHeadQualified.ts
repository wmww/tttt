// TS2352's head qualifies two types printed alike (reportRelationError's getTypeNamesForErrorDisplay: vitest's Locator)
// @filename: a.ts
export interface Locator { a: number }
// @filename: b.ts
import * as A from "./a";
interface Locator { b: string; c: number }
declare const l: A.Locator;
const x = l as Locator;
