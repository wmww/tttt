//// [test/cases/ttttDeclarationMapRenamedStringName.ts] ////

//// [ttttDeclarationMapRenamedStringName.ts]
// An object literal's string literal property name printed otherwise (`'a'` as `a`, `"a-b"` requoted) keeps the
// name's mappings (tsgo's reuseName keeps the range: query's codemod fixtures)
export const f = () => {
  return {
    'useErrorBoundary': true,
    "a-b": 1,
    'c': 'x',
  }
}


//// [ttttDeclarationMapRenamedStringName.js]
// An object literal's string literal property name printed otherwise (`'a'` as `a`, `"a-b"` requoted) keeps the
// name's mappings (tsgo's reuseName keeps the range: query's codemod fixtures)
export const f = () => {
    return {
        'useErrorBoundary': true,
        "a-b": 1,
        'c': 'x',
    };
};


//// [ttttDeclarationMapRenamedStringName.d.ts]
export declare const f: () => {
    useErrorBoundary: boolean;
    "a-b": number;
    c: string;
};
//# sourceMappingURL=ttttDeclarationMapRenamedStringName.d.ts.map