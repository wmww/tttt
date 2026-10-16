//// [test/cases/ttttDeclarationMapConstEmptyTuple.ts] ////

//// [ttttDeclarationMapConstEmptyTuple.ts]
// An `as const` object with an empty array is its pseudo type too (the empty tuple is its tuple target, no
// reference): the property names are mapped like tsgo's (pi's tool prompt contributions)
export const c = {
	snippet: "List",
} as const;
export const e = {
	g: [],
} as const;
export const f = {
	g: [1],
} as const;


//// [ttttDeclarationMapConstEmptyTuple.js]
// An `as const` object with an empty array is its pseudo type too (the empty tuple is its tuple target, no
// reference): the property names are mapped like tsgo's (pi's tool prompt contributions)
export const c = {
    snippet: "List",
};
export const e = {
    g: [],
};
export const f = {
    g: [1],
};


//// [ttttDeclarationMapConstEmptyTuple.d.ts]
export declare const c: {
    readonly snippet: "List";
};
export declare const e: {
    readonly g: readonly [];
};
export declare const f: {
    readonly g: readonly [1];
};
//# sourceMappingURL=ttttDeclarationMapConstEmptyTuple.d.ts.map