// @declaration: true
// @emitDeclarationOnly: true
// @strict: true
// A property spread from a namespace whose export is an alias (`export { isEven }` of an import) is the alias symbol,
// with no value declaration: tsgo serializes its type (a top-level arrow's or function's `typeof ns.x`), neither the
// variable's annotations nor a method (vest's allRules). Exports declared in the module itself still reuse them

// @filename: rules/isEven.ts
export const isEven = (value: string | number): boolean => typeof value === 'number';
export function isOdd(v: number): boolean { return v % 2 === 1; }
export const plain = 1;

// @filename: rules/numberRules.ts
import { isEven, isOdd, plain } from './isEven';
export { isEven, isOdd, plain };

// @filename: all.ts
import * as numberRules from './rules/numberRules';
import * as direct from './rules/isEven';
export const a1 = { ...numberRules } as const;
export const a2 = { ...numberRules };
export const a3 = { ...direct } as const;
export const a4 = { ...direct };
export const a5 = { ...a2, x: 1 };
