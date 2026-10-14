// @captureSuggestions: true
// References to a declaration whose JSDoc says @deprecated are TS6385 suggestions (tsc's checkIdentifier)
/** @deprecated */
declare const oldValue: { value: number };
export const a = oldValue.value;
export enum Values {
    [oldValue.value] = 1,
}
/**
 * Not this one: the tag is in the text
 * see the deprecated notes
 */
declare const fine: number;
export const b = fine;
/** @deprecated use something else */
declare let older: string;
export const c = older;
