declare const tag: unique symbol;
type TagContainer<Token> = { readonly [tag]: Token };
type UUID = string & TagContainer<'UUID'>;
type D = Record<UUID, {bar: string}>;
declare const d: D;
const id = 'x' as UUID;
const j = d[id];
type K = keyof D;
