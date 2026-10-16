//// [test/cases/ttttDeclarationMapComputedIndexMembers.ts] ////

//// [ttttDeclarationMapComputedIndexMembers.ts]
// An object literal's computed entity names that only make an index signature print as members (tsc's
// indexInfoToObjectComputedNamesOrSignatureDeclaration), mapped like tsgo's reused members: the member, `[`, each
// name, `]` (mcp-sdk's OAUTH_ERRORS)
export class E { static errorCode: string; }
export class A extends E { static override errorCode = 'a'; }
export class B extends E { static override errorCode = 'b'; }
export const ERRS = {
    [A.errorCode]: A,
    [B.errorCode]: B,
} as const;
export const PLAIN = {
    [A.errorCode]: A,
} as const;


//// [ttttDeclarationMapComputedIndexMembers.js]
// An object literal's computed entity names that only make an index signature print as members (tsc's
// indexInfoToObjectComputedNamesOrSignatureDeclaration), mapped like tsgo's reused members: the member, `[`, each
// name, `]` (mcp-sdk's OAUTH_ERRORS)
export class E {
    static errorCode;
}
export class A extends E {
    static errorCode = 'a';
}
export class B extends E {
    static errorCode = 'b';
}
export const ERRS = {
    [A.errorCode]: A,
    [B.errorCode]: B,
};
export const PLAIN = {
    [A.errorCode]: A,
};


//// [ttttDeclarationMapComputedIndexMembers.d.ts]
export declare class E {
    static errorCode: string;
}
export declare class A extends E {
    static errorCode: string;
}
export declare class B extends E {
    static errorCode: string;
}
export declare const ERRS: {
    readonly [A.errorCode]: typeof A;
    readonly [B.errorCode]: typeof B;
};
export declare const PLAIN: {
    readonly [A.errorCode]: typeof A;
};
//# sourceMappingURL=ttttDeclarationMapComputedIndexMembers.d.ts.map