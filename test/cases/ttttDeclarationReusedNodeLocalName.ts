// @declaration: true
// @declarationMap: true
// @strict: true
// @noEmitOnError: false
// A reused annotation (a returned `as` type) naming a body-local alias: only that reference is printed as its type,
// the rest of the node is reused and keeps its map (tsgo's visitExistingNodeTreeSymbols recovers at the failing type
// reference); telegraf's `entities()`. A local alias whose type can't be printed (cyclic) fails the whole reuse
// @filename: entities.ts
export interface Entity { offset: number; type: string }
export class Context {
    entities<T extends string[]>(...types: T) {
        type Selected = T extends [] ? string : T[number];
        return [] as (Entity & { type: Selected; fragment: string })[];
    }
}
// @filename: recursive.ts
export function recursive() {
    type Recursive = Recursive[];
    return null as unknown as { r: Recursive };
}
