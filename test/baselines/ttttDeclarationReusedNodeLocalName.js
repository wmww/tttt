//// [test/cases/ttttDeclarationReusedNodeLocalName.ts] ////

//// [entities.ts]
export interface Entity { offset: number; type: string }
export class Context {
    entities<T extends string[]>(...types: T) {
        type Selected = T extends [] ? string : T[number];
        return [] as (Entity & { type: Selected; fragment: string })[];
    }
}
//// [recursive.ts]
export function recursive() {
    type Recursive = Recursive[];
    return null as unknown as { r: Recursive };
}


//// [entities.js]
export class Context {
    entities(...types) {
        return [];
    }
}
//// [recursive.js]
export function recursive() {
    return null;
}


//// [entities.d.ts]
export interface Entity {
    offset: number;
    type: string;
}
export declare class Context {
    entities<T extends string[]>(...types: T): (Entity & {
        type: T extends [] ? string : T[number];
        fragment: string;
    })[];
}
//# sourceMappingURL=entities.d.ts.map