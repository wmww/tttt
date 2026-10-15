//// [test/cases/ttttDeclarationLateBoundMemberPaints.ts] ////

//// [em.ts]
export class EntityManager<D> { d!: D }
//// [types.ts]
export declare const EntityManagerType: unique symbol;
//// [driver.ts]
import { EntityManagerType } from "./types";
import { EntityManager } from "./em";
export abstract class DatabaseDriver {
    [EntityManagerType]!: EntityManager<this>;
    get [Symbol.iterator](): () => EntityManager<number> { return null!; }
}


//// [em.js]
export class EntityManager {
    d;
}
//// [types.js]
export {};
//// [driver.js]
import { EntityManagerType } from "./types";
export class DatabaseDriver {
    [EntityManagerType];
    get [Symbol.iterator]() { return null; }
}


//// [em.d.ts]
export declare class EntityManager<D> {
    d: D;
}
//// [types.d.ts]
export declare const EntityManagerType: unique symbol;
//// [driver.d.ts]
import { EntityManagerType } from "./types";
import { EntityManager } from "./em";
export declare abstract class DatabaseDriver {
    [EntityManagerType]: EntityManager<this>;
    get [Symbol.iterator](): () => EntityManager<number>;
}
