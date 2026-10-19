// A destructuring declaration without initializers is written whole, typed by its initializer (tsgo's
// transformVariableDeclaration: only a pattern with initializers is recreated element by element). Its elements' own
// types aren't printed, so `types`' `typeof import("pg-types")`, which no specifier reaches but through a nested
// node_modules, is no TS2883 (atproto's `const { Pool: PgPool, types: pgTypes } = pg`)
// @declaration: true
// @module: nodenext
// @strict: true
// @esModuleInterop: true
// @filename: /node_modules/pg/package.json
{ "name": "pg", "types": "index.d.ts" }
// @filename: /node_modules/pg/index.d.ts
import pgTypes = require("pg-types");
export class Pool { x: number }
export const types: typeof pgTypes;
// @filename: /node_modules/pg/node_modules/pg-types/package.json
{ "name": "pg-types", "types": "index.d.ts" }
// @filename: /node_modules/pg/node_modules/pg-types/index.d.ts
export function getTypeParser(oid: number): (s: string) => any;
// @filename: /src/db.ts
import pg from 'pg'
const { Pool: PgPool, types: pgTypes } = pg
type PgPool = InstanceType<typeof PgPool>
export class Database { pool!: PgPool }
export const t = pgTypes.getTypeParser(1);
