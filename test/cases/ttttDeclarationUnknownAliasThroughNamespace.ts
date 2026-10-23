// @declaration: true
// @strict: true
// @noEmitOnError: false
// tsc's unknownSymbol (a name imported from a module that doesn't resolve) is named through an accessible alias that
// resolves to it, also one among the exports of a namespace import in scope (getAccessibleSymbolChainFromSymbolTable
// looking into an alias's exported members): vest's `arrayRules.isEmpty<...>` for vest-utils' unresolved DropFirst
// @filename: rules/arrayRules.ts
import { isEmpty } from 'missing-utils';
export function equals(value: unknown[], other: unknown[]): boolean { return value === other; }
export { isEmpty };
// @filename: rules/builder.ts
import type { DropFirst } from 'missing-utils';
export declare function build<R extends Record<string, (...args: any[]) => any>>(r: R): {
  [K in keyof R]: (...args: DropFirst<Parameters<R[K]>>) => number;
};
// @filename: lazy.ts
import * as arrayRules from './rules/arrayRules';
import { build } from './rules/builder';
export const e = build({ equals: arrayRules.equals });
