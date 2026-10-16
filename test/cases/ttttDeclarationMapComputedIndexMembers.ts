// @declaration: true
// @declarationMap: true
// @target: es2022
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
