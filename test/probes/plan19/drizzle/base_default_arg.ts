// @strict: true
// A base class reference that leaves out a type argument whose default mentions an earlier parameter: the default
// is instantiated with the arguments before it (tsc's fillMissingTypeArguments)
// @filename: db.ts
type Ex<T extends Record<string, unknown>> = { [K in keyof T as T[K] extends number ? K : never]: T[K] extends number ? { n: T[K], k: K & string } : never };
export class Db<Q, TFull extends Record<string, unknown> = Record<string, never>, TSchema extends Record<string, unknown> = Ex<TFull>> {
    query!: TFull extends Record<string, never> ? "missing" : { [K in keyof TSchema]: TSchema[K] };
}
// @filename: main.ts
import { Db } from "./db";
declare class JsDb<TSchema extends Record<string, unknown> = Record<string, never>> extends Db<1, TSchema> {}
declare const db: JsDb<{ users: 1 }>;
const q = db.query;
const u = db.query.users;
