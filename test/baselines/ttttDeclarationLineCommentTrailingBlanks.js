//// [test/cases/ttttDeclarationLineCommentTrailingBlanks.ts] ////

//// [ttttDeclarationLineCommentTrailingBlanks.ts]
// A JSDoc comment followed by a line comment with trailing blanks still leads the declaration (markdown-magic: a line
// comment's range takes in the blanks after it)

/** doc */
// c   
export const o = 1;

/**
 * Doc.
 */
// TODO code sections 
export function f() {}




//// [ttttDeclarationLineCommentTrailingBlanks.d.ts]
/** doc */
export declare const o = 1;
/**
 * Doc.
 */
export declare function f(): void;
