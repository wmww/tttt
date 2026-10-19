//// [test/cases/ttttDeclarationSpreadAliasExports.ts] ////

//// [isEven.ts]
export const isEven = (value: string | number): boolean => typeof value === 'number';
export function isOdd(v: number): boolean { return v % 2 === 1; }
export const plain = 1;

//// [numberRules.ts]
import { isEven, isOdd, plain } from './isEven';
export { isEven, isOdd, plain };

//// [all.ts]
import * as numberRules from './rules/numberRules';
import * as direct from './rules/isEven';
export const a1 = { ...numberRules } as const;
export const a2 = { ...numberRules };
export const a3 = { ...direct } as const;
export const a4 = { ...direct };
export const a5 = { ...a2, x: 1 };




//// [isEven.d.ts]
export declare const isEven: (value: string | number) => boolean;
export declare function isOdd(v: number): boolean;
export declare const plain = 1;
//// [numberRules.d.ts]
import { isEven, isOdd, plain } from './isEven';
export { isEven, isOdd, plain };
//// [all.d.ts]
import * as numberRules from './rules/numberRules';
export declare const a1: {
    readonly isEven: typeof numberRules.isEven;
    readonly isOdd: typeof numberRules.isOdd;
    readonly plain: 1;
};
export declare const a2: {
    isEven: typeof numberRules.isEven;
    isOdd: typeof numberRules.isOdd;
    plain: 1;
};
export declare const a3: {
    readonly isEven: (value: string | number) => boolean;
    readonly isOdd: typeof numberRules.isOdd;
    readonly plain: 1;
};
export declare const a4: {
    isEven: (value: string | number) => boolean;
    isOdd(v: number): boolean;
    plain: 1;
};
export declare const a5: {
    isEven: typeof numberRules.isEven;
    isOdd: typeof numberRules.isOdd;
    plain: 1;
    x: number;
};
