// @declaration: true
// @strict: true
// A generic alias whose body is an indexed access resolving to a method's function type, referenced through an import:
// tsc's getTypeFromTypeAliasReference names the instance by the import's alias (getObjectTypeInstantiation with the
// alias), so it prints as FunctionComponent<X>; referenced locally it has no name and prints its signature

// @filename: t.ts
export interface DevJSX { fileName: string }
export type JSXOutput = string | null;
export type FunctionComponent<P = unknown> = {
  renderFn(props: P, key: string | null, flags: number, dev?: DevJSX): JSXOutput;
}['renderFn'];
export declare function rs<T>(key: string, factory: () => T): T;
export const local = rs<FunctionComponent<{ data: string }>>('l', () => () => null);

// @filename: a.ts
import type { FunctionComponent } from './t';
import { rs } from './t';
export const SSR2 = rs<FunctionComponent<{ data: string }>>('SSRRaw', () => () => null);
declare const fc: FunctionComponent<{ data: string }>;
export const SSR3 = fc;
export const both = [SSR2, fc];
