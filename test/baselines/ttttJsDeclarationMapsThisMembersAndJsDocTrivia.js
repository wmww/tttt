//// [test/cases/ttttJsDeclarationMapsThisMembersAndJsDocTrivia.ts] ////

//// [a.js]
export class MainMenu {
  constructor() {
    /** @type {string} */
    // @ts-ignore
    this.pageTree = undefined;
    this.currentPath = '';
    this["a-b"] = 1;
  }
}

/**
 * @param {string} a
 * @param {Object} [options]
 * @param {string} [options.title]
 * @param {number} [options.menu] render
 */
export function document(a, { title, menu } = {}) {}

/**
 * @param {number} [b]
 */
export function h(b = 1) {}

/**
 * @typedef {{
 *   value: string
 * }} ValueObject
 */

/**
 * @typedef {{
 *   m(): void,
 *   (): void,
 *   readonly x: number,
 *   new (): string,
 *   props: {
 *     [key: string]: ValueObject|null,
 *   },
 *   exported?: boolean
 * }} T
 */

/**
 * @param {T} x
 * @returns {ValueObject}
 */
export function f(x) { return { value: "" }; }

/** @type {(a: string, b: number
 * ) => void} */
export const g = (a, b) => {};




//// [a.d.ts]
export declare class MainMenu {
    /** @type {string} */
    pageTree: string;
    currentPath: string;
    "a-b": number;
    constructor();
}
/**
 * @param {string} a
 * @param {Object} [options]
 * @param {string} [options.title]
 * @param {number} [options.menu] render
 */
export declare function document(a: string, { title, menu }?: {
    title?: string;
    menu?: number;
}): void;
/**
 * @param {number} [b]
 */
export declare function h(b?: number): void;
export type ValueObject = {
    value: string;
};
export type T = {
    m(): void;
    (): void;
    readonly x: number;
    new (): string;
    props: {
        [key: string]: ValueObject | null;
    };
    exported?: boolean;
};
/**
 * @typedef {{
 *   value: string
 * }} ValueObject
 */
/**
 * @typedef {{
 *   m(): void,
 *   (): void,
 *   readonly x: number,
 *   new (): string,
 *   props: {
 *     [key: string]: ValueObject|null,
 *   },
 *   exported?: boolean
 * }} T
 */
/**
 * @param {T} x
 * @returns {ValueObject}
 */
export declare function f(x: T): ValueObject;
/** @type {(a: string, b: number
 * ) => void} */
export declare const g: (a: string, b: number) => void;
//# sourceMappingURL=a.d.ts.map