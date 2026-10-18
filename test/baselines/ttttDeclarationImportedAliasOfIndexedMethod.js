//// [test/cases/ttttDeclarationImportedAliasOfIndexedMethod.ts] ////

//// [t.ts]
export interface DevJSX { fileName: string }
export type JSXOutput = string | null;
export type FunctionComponent<P = unknown> = {
  renderFn(props: P, key: string | null, flags: number, dev?: DevJSX): JSXOutput;
}['renderFn'];
export declare function rs<T>(key: string, factory: () => T): T;
export const local = rs<FunctionComponent<{ data: string }>>('l', () => () => null);

//// [a.ts]
import type { FunctionComponent } from './t';
import { rs } from './t';
export const SSR2 = rs<FunctionComponent<{ data: string }>>('SSRRaw', () => () => null);
declare const fc: FunctionComponent<{ data: string }>;
export const SSR3 = fc;
export const both = [SSR2, fc];


//// [t.js]
export const local = rs('l', () => () => null);
//// [a.js]
import { rs } from './t';
export const SSR2 = rs('SSRRaw', () => () => null);
export const SSR3 = fc;
export const both = [SSR2, fc];


//// [t.d.ts]
export interface DevJSX {
    fileName: string;
}
export type JSXOutput = string | null;
export type FunctionComponent<P = unknown> = {
    renderFn(props: P, key: string | null, flags: number, dev?: DevJSX): JSXOutput;
}['renderFn'];
export declare function rs<T>(key: string, factory: () => T): T;
export declare const local: (props: {
    data: string;
}, key: string | null, flags: number, dev?: DevJSX) => JSXOutput;
//// [a.d.ts]
import type { FunctionComponent } from './t';
export declare const SSR2: FunctionComponent<{
    data: string;
}>;
export declare const SSR3: FunctionComponent<{
    data: string;
}>;
export declare const both: FunctionComponent<{
    data: string;
}>[];
