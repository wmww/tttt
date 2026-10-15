//// [test/cases/ttttDeclarationMapRenamedName.ts] ////

//// [ttttDeclarationMapRenamedName.ts]
// A computed literal name renamed by its text keeps the computed name's range in the map
export class L { m() { return { ['_rolumns']: [] as never[], ['a b']: 1 }; } }


//// [ttttDeclarationMapRenamedName.js]
// A computed literal name renamed by its text keeps the computed name's range in the map
export class L {
    m() { return { ['_rolumns']: [], ['a b']: 1 }; }
}


//// [ttttDeclarationMapRenamedName.d.ts]
export declare class L {
    m(): {
        _rolumns: never[];
        "a b": number;
    };
}
//# sourceMappingURL=ttttDeclarationMapRenamedName.d.ts.map