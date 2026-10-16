// @declaration: true
// @strict: true
// @target: es2022
// @module: esnext
// A mapped type printed inside itself is named by the alias that declares it, without type arguments (tsgo's
// getTypeAliasForTypeLiteral: the mapped type's declaration is Pick's), not the cyclic structure placeholder (TS5088),
// whatever alias the instantiation goes by (`Omit` here, a local the declaration can't name); unparenthesized
// (mermaid's fastdom.ts)

// @filename: fastdom.d.ts
type Omit<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>>;

declare class Fastdom {
  clear<T extends () => void>(task: T): boolean;
  extend<T extends object>(props: T): Omit<this, keyof T & keyof this> & T;
  catch: null | ((e: unknown) => any);
}

declare const fastdom: Fastdom

export default fastdom;

// @filename: a.ts
import fastdomModule from './fastdom';
export const fastdom = fastdomModule.extend({ raf(cb: () => void) {} });
