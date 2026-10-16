//// [test/cases/ttttDeclarationUnionAnnotationIdenticalButAlias.ts] ////

//// [ttttDeclarationUnionAnnotationIdenticalButAlias.ts]
// A property of an intersection, `{ id: Id } & { id?: Id }`, has the type the intersection of its declarations' types
// makes, `string | number | null` without the alias: its first declaration's annotation `NS.Id` still says it (tsgo's
// pseudoTypeEquivalentToType compares unions identical but for the alias), so the spread prints it (trpc's
// transformer.d.ts printed `id: string | number | null`)
export namespace NS {
  export type Id = number | string | null;
  export interface Base { id?: Id; jsonrpc?: '2.0'; }
  export interface Err extends Base { error: string; }
  export interface Res extends Base { result: number; }
}
export type Msg = { id: NS.Id } & (NS.Err | NS.Res);
export function spread(response: Msg) {
  return { ...response };
}


//// [ttttDeclarationUnionAnnotationIdenticalButAlias.js]
export function spread(response) {
    return { ...response };
}


//// [ttttDeclarationUnionAnnotationIdenticalButAlias.d.ts]
export declare namespace NS {
    type Id = number | string | null;
    interface Base {
        id?: Id;
        jsonrpc?: '2.0';
    }
    interface Err extends Base {
        error: string;
    }
    interface Res extends Base {
        result: number;
    }
}
export type Msg = {
    id: NS.Id;
} & (NS.Err | NS.Res);
export declare function spread(response: Msg): {
    jsonrpc?: '2.0';
    error: string;
    id: NS.Id;
} | {
    jsonrpc?: '2.0';
    result: number;
    id: NS.Id;
};
