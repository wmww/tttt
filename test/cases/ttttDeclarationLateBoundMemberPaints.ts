// A class member with a late bound name (`[sym]` of a unique symbol) is emitted, so its type is checked like any other
// member's: the imports it names are painted (mikro-orm's DatabaseDriver dropped `import { EntityManager }` for
// `[EntityManagerType]: EntityManager<this>`)
// @declaration: true
// @filename: /em.ts
export class EntityManager<D> { d!: D }
// @filename: /types.ts
export declare const EntityManagerType: unique symbol;
// @filename: /driver.ts
import { EntityManagerType } from "./types";
import { EntityManager } from "./em";
export abstract class DatabaseDriver {
    [EntityManagerType]!: EntityManager<this>;
    get [Symbol.iterator](): () => EntityManager<number> { return null!; }
}
