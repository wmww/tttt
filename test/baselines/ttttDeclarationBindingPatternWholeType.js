//// [test/cases/ttttDeclarationBindingPatternWholeType.ts] ////

//// [package.json]
{ "name": "pg", "types": "index.d.ts" }
//// [index.d.ts]
import pgTypes = require("pg-types");
export class Pool { x: number }
export const types: typeof pgTypes;
//// [package.json]
{ "name": "pg-types", "types": "index.d.ts" }
//// [index.d.ts]
export function getTypeParser(oid: number): (s: string) => any;
//// [db.ts]
import pg from 'pg'
const { Pool: PgPool, types: pgTypes } = pg
type PgPool = InstanceType<typeof PgPool>
export class Database { pool!: PgPool }
export const t = pgTypes.getTypeParser(1);


//// [db.js]
"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.t = exports.Database = void 0;
const pg_1 = __importDefault(require("pg"));
const { Pool: PgPool, types: pgTypes } = pg_1.default;
class Database {
    pool;
}
exports.Database = Database;
exports.t = pgTypes.getTypeParser(1);


//// [db.d.ts]
import pg from 'pg';
declare const { Pool: PgPool, types: pgTypes }: typeof pg;
type PgPool = InstanceType<typeof PgPool>;
export declare class Database {
    pool: PgPool;
}
export declare const t: (s: string) => any;
export {};
