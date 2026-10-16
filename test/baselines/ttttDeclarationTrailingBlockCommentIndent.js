//// [test/cases/ttttDeclarationTrailingBlockCommentIndent.ts] ////

//// [ttttDeclarationTrailingBlockCommentIndent.ts]
// A trailing block comment's later lines are re-indented by the writer's indent like a leading one's (tsc's
// writeCommentRangeWorker: indent - first line's column + the line's column; effect's STM.d.ts)
export interface I {
    a: string; /* first
 second */
    b: number;
}
export declare const x: {
    c: string; /**
 * doc
 */
};
export namespace N {
    export const y = 1; /* one
   two */
}


//// [ttttDeclarationTrailingBlockCommentIndent.js]
export var N;
(function (N) {
    N.y = 1; /* one
   two */
})(N || (N = {}));


//// [ttttDeclarationTrailingBlockCommentIndent.d.ts]
export interface I {
    a: string;
    b: number;
}
export declare const x: {
    c: string; /**
 * doc
 */
};
export declare namespace N {
    const y = 1;
}
