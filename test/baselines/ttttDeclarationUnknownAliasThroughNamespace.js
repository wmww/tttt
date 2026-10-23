//// [test/cases/ttttDeclarationUnknownAliasThroughNamespace.ts] ////

//// [arrayRules.ts]
import { isEmpty } from 'missing-utils';
export function equals(value: unknown[], other: unknown[]): boolean { return value === other; }
export { isEmpty };
//// [builder.ts]
import type { DropFirst } from 'missing-utils';
export declare function build<R extends Record<string, (...args: any[]) => any>>(r: R): {
  [K in keyof R]: (...args: DropFirst<Parameters<R[K]>>) => number;
};
//// [lazy.ts]
import * as arrayRules from './rules/arrayRules';
import { build } from './rules/builder';
export const e = build({ equals: arrayRules.equals });


//// [arrayRules.js]
import { isEmpty } from 'missing-utils';
export function equals(value, other) { return value === other; }
export { isEmpty };
//// [builder.js]
export {};
//// [lazy.js]
import * as arrayRules from './rules/arrayRules';
import { build } from './rules/builder';
export const e = build({ equals: arrayRules.equals });


//// [arrayRules.d.ts]
import { isEmpty } from 'missing-utils';
export declare function equals(value: unknown[], other: unknown[]): boolean;
export { isEmpty };
//// [builder.d.ts]
import type { DropFirst } from 'missing-utils';
export declare function build<R extends Record<string, (...args: any[]) => any>>(r: R): {
    [K in keyof R]: (...args: DropFirst<Parameters<R[K]>>) => number;
};
//// [lazy.d.ts]
import * as arrayRules from './rules/arrayRules';
export declare const e: {
    equals: (...args: arrayRules.isEmpty<Parameters<typeof arrayRules.equals>>) => number;
};
