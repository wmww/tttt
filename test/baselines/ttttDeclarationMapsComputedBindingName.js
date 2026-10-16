//// [test/cases/ttttDeclarationMapsComputedBindingName.ts] ////

//// [a.ts]
interface R { strings: string[]; _$litType$: number; [k: string]: unknown }
const k = "x";
export class Template {
  constructor(
    // A comment
    {strings, ['_$litType$']: type}: R,
    options?: number
  ) {}
}
export function f({ [k]: x, [0]: y }: R) {}


//// [a.js]
const k = "x";
export class Template {
    constructor(
    // A comment
    { strings, ['_$litType$']: type }, options) { }
}
export function f({ [k]: x, [0]: y }) { }


//// [a.d.ts]
interface R {
    strings: string[];
    _$litType$: number;
    [k: string]: unknown;
}
declare const k = "x";
export declare class Template {
    constructor({ strings, ['_$litType$']: type }: R, options?: number);
}
export declare function f({ [k]: x, [0]: y }: R): void;
export {};
//# sourceMappingURL=a.d.ts.map