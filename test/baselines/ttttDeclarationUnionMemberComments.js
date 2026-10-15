//// [test/cases/ttttDeclarationUnionMemberComments.ts] ////

//// [ttttDeclarationUnionMemberComments.ts]
// Comments on the lines between a union member and the `|` after it (emitListItems' leading comments at the
// previous element's end): written before the delimiter
export type K =
	/** first */
	| 'match'
	/** second */
	| 'mismatch'
	// line comment
	| 'x' /** same line */
	| 'y';
export namespace N {
	export type T =
		| { a: 1 }
		/** obj */
		| { b: 2 };
}


//// [ttttDeclarationUnionMemberComments.js]
export {};


//// [ttttDeclarationUnionMemberComments.d.ts]
export type K = 
/** first */
'match'
/** second */
 | 'mismatch' | 'x' /** same line */ | 'y';
export declare namespace N {
    type T = {
        a: 1;
    }
    /** obj */
     | {
        b: 2;
    };
}
