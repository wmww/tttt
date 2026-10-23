// @declaration: true
// @strict: true
// A spread copy of a mapped type's property (Partial<Data>'s) has no valueDeclaration in tsc (getSpreadSymbol keeps the
// mapped symbol): the declaration file doesn't copy the original member's comments (feathers' generators)
interface Data {
    /** The name */
    name: string;
    /** A description */
    description: string;
}
interface Base {
    /** The cwd */
    cwd: string;
}
declare const args: Base & Partial<Data>;
export const spread = { ...args, extra: 1 };
declare const p: Partial<Data>;
export const spreadMapped = { ...p };
