//// [test/cases/ttttDeclarationSpreadMappedNoComments.ts] ////

//// [ttttDeclarationSpreadMappedNoComments.ts]
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


//// [ttttDeclarationSpreadMappedNoComments.js]
export const spread = { ...args, extra: 1 };
export const spreadMapped = { ...p };


//// [ttttDeclarationSpreadMappedNoComments.d.ts]
export declare const spread: {
    name?: string | undefined;
    description?: string | undefined;
    /** The cwd */
    cwd: string;
    extra: number;
};
export declare const spreadMapped: {
    name?: string | undefined;
    description?: string | undefined;
};
