// A property of an intersection, `{ id: Id } & { id?: Id }`, has the type the intersection of its declarations' types
// makes, `string | number | null` without the alias: its first declaration's annotation `NS.Id` still says it (tsgo's
// pseudoTypeEquivalentToType compares unions identical but for the alias), so the spread prints it (trpc's
// transformer.d.ts printed `id: string | number | null`)
// @strict: true
// @declaration: true
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
