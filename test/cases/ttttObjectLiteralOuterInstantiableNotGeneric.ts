// A reduction of drizzle's proxy migrators under the non-strict flags (flagflip sloppy): `db.select({}).from(...).orderBy(...).limit(1)`
// in a generic function. An object literal there is instantiated by the type parameters declared around it
// (ae4459588), but that must not make it a generic type: TYF_COULD_CONTAIN_TYPE_VARIABLES doubles as "generic" in
// tttt, and the builder chain lost its index-signature property (TS2532/TS2722/TS4111). The reduced code is broken
// TypeScript; what matters is that tsgo's errors match
// @filename: drizzle-orm/tsconfig.json
{"compilerOptions": {"allowJs": true, "allowImportingTsExtensions": true, "allowUnreachableCode": false, "allowUnusedLabels": false, "checkJs": true, "exactOptionalPropertyTypes": false, "forceConsistentCasingInFileNames": true, "isolatedModules": true, "lib": ["es2022", "es2020", "es2018", "es2017", "es7", "es6", "es5"], "module": "esnext", "moduleResolution": "bundler", "noFallthroughCasesInSwitch": true, "noImplicitAny": false, "noImplicitThis": false, "noImplicitReturns": true, "noPropertyAccessFromIndexSignature": true, "noUncheckedIndexedAccess": true, "noImplicitOverride": true, "preserveConstEnums": true, "skipLibCheck": true, "strict": false, "strictBindCallApply": false, "strictBuiltinIteratorReturn": false, "strictFunctionTypes": false, "strictNullChecks": false, "strictPropertyInitialization": false, "stripInternal": true, "target": "esnext", "useUnknownInCatchVariables": false, "allowSyntheticDefaultImports": true, "alwaysStrict": true, "esModuleInterop": true, "paths": {"~/*": ["./src/*"]}, "noEmit": true}, "files": ["src/mysql-proxy/migrator.ts"]}
// @filename: drizzle-orm/package.json
{"name": "drizzle-orm", "version": "0.45.3", "type": "module", "main": "./index.cjs", "module": "./index.js", "types": "./index.d.ts"}
// @filename: package.json
{"name": "drizzle-root"}
// @filename: drizzle-orm/src/mysql-core/db.ts
import {
	MySqlSelectBuilder,
} from './query-builders/index.ts';
export class MySqlDatabase<
	TPreparedQueryHKT extends PreparedQueryHKTBase,
	TFullSchema extends Record<string, unknown> = {},
	TSchema extends TablesRelationalConfig = ExtractTablesWithRelations<TFullSchema>,
> {
	select<TSelection extends SelectedFields>(fields: TSelection): MySqlSelectBuilder<TSelection, TPreparedQueryHKT>;
	/**
	 */
};
// @filename: drizzle-orm/src/mysql-core/query-builders/index.ts
export * from './select.ts';
// @filename: drizzle-orm/src/mysql-core/query-builders/select.ts
import type {
	BuildSubquerySelection,
	GetSelectTableName,
} from '~/query-builders/select.types.ts';
import type {
	CreateMySqlSelectFromBuilderMode,
	MySqlSelectHKT,
	MySqlSelectWithout,
} from './select.types.ts';
export class MySqlSelectBuilder<
	TSelection extends SelectedFields | undefined,
	TPreparedQueryHKT extends PreparedQueryHKTBase,
	TBuilderMode extends 'db' | 'qb' = 'db',
> {
	from<TFrom extends MySqlTable | Subquery | MySqlViewBase | SQL>(
		source: TFrom,
	): CreateMySqlSelectFromBuilderMode<
		TBuilderMode,
		GetSelectTableName<TFrom>,
		TSelection extends undefined ? GetSelectTableSelection<TFrom> : TSelection,
		TSelection extends undefined ? 'single' : 'partial',
		TPreparedQueryHKT
	> {
	}
}
export abstract class MySqlSelectQueryBuilderBase<
	THKT extends MySqlSelectHKTBase,
	TTableName extends string | undefined,
	TPreparedQueryHKT extends PreparedQueryHKTBase,
	TNullabilityMap extends Record<string, JoinNullability> = TTableName extends string ? Record<TTableName, 'not-null'>
		: {},
	TDynamic extends boolean = false,
	TExcludedMethods extends string = never,
> extends TypedQueryBuilder<TSelectedFields, TResult> {
	override readonly _: {
		readonly hkt: THKT;
		readonly tableName: TTableName;
		readonly excludedMethods: TExcludedMethods;
	};
	private createJoin<
	>(
	): 'cross' extends TJoinType ? MySqlCrossJoinFn<this, TDynamic, TIsLateral>
		: MySqlJoinFn<this, TDynamic, TJoinType, TIsLateral>
	/**
	 */
	orderBy(...columns: (MySqlColumn | SQL | SQL.Aliased)[]): MySqlSelectWithout<this, TDynamic, 'orderBy'>;
}
export interface MySqlSelectBase<
	TTableName extends string | undefined,
	TSelection extends ColumnsSelection,
	TSelectMode extends SelectMode,
	TPreparedQueryHKT extends PreparedQueryHKTBase,
	TNullabilityMap extends Record<string, JoinNullability> = TTableName extends string ? Record<TTableName, 'not-null'>
		: {},
	TDynamic extends boolean = false,
	TExcludedMethods extends string = never,
	TResult extends any[] = SelectResult<TSelection, TSelectMode, TNullabilityMap>[],
	TSelectedFields extends ColumnsSelection = BuildSubquerySelection<TSelection, TNullabilityMap>,
> extends
	MySqlSelectQueryBuilderBase<
		MySqlSelectHKT,
		TTableName,
		TSelection,
		TSelectMode,
		TSelectedFields
	>,
{}
export class MySqlSelectBase<
> {
}
// @filename: drizzle-orm/src/mysql-core/query-builders/select.types.ts
import type { IndexConfig, MySqlSelectBase, MySqlSelectQueryBuilderBase } from './select.ts';
export type BuildAliasTable<TTable extends MySqlTable | View, TAlias extends string> = TTable extends Table
	? MySqlTableWithColumns<
	>
	: never;
export type MySqlJoin<
> = T extends any ? MySqlSelectWithout<
		any
	>
	: never;
export type MySqlJoinFn<
> = <
>(
) => MySqlJoin<T, TDynamic, 'cross', TJoinedTable, TJoinedName>;
export interface MySqlSelectHKTBase {
	tableName: string | undefined;
}
export type MySqlSelectKind<
	T extends MySqlSelectHKTBase,
	TTableName extends string | undefined,
	TSelectMode extends SelectMode,
	TPreparedQueryHKT extends PreparedQueryHKTBase,
	TNullabilityMap extends Record<string, JoinNullability>,
	TDynamic extends boolean,
	TExcludedMethods extends string,
> = (T & {
	tableName: TTableName;
	excludedMethods: TExcludedMethods;
})['_type'];
export interface MySqlSelectHKT extends MySqlSelectHKTBase {
	_type: MySqlSelectBase<
		this['tableName'],
		Assume<this['selection'], ColumnsSelection>,
		this['selectMode'],
		Assume<this['selectedFields'], ColumnsSelection>
	>;
}
export type MySqlSelectWithout<
	T extends AnyMySqlSelectQueryBuilder,
	TDynamic extends boolean,
	K extends keyof T & string,
> = TDynamic extends true ? T : Omit<
	MySqlSelectKind<
		T['_']['hkt'],
		T['_']['tableName'],
		T['_']['selection'],
		T['_']['selectMode'],
		T['_']['preparedQueryHKT'],
		T['_']['nullabilityMap'],
		TDynamic,
	>,
	TResetExcluded extends true ? K : T['_']['excludedMethods'] | K
>;
export type CreateMySqlSelectFromBuilderMode<
	TBuilderMode extends 'db' | 'qb',
	TTableName extends string | undefined,
	TSelection extends ColumnsSelection,
	TSelectMode extends SelectMode,
	TPreparedQueryHKT extends PreparedQueryHKTBase,
> = TBuilderMode extends 'db' ? MySqlSelectBase<TTableName, TSelection, TSelectMode, TPreparedQueryHKT>
	: MySqlSelectQueryBuilderBase<MySqlSelectQueryBuilderHKT, TTableName, TSelection, TSelectMode, TPreparedQueryHKT>;
export type MySqlSelect<
> = MySqlSelectBase<TTableName, TSelection, TSelectMode, PreparedQueryHKTBase, TNullabilityMap, true, never>;
// @filename: drizzle-orm/src/mysql-proxy/driver.ts
import { MySqlDatabase } from '~/mysql-core/db.ts';
export class MySqlRemoteDatabase<
	TSchema extends Record<string, unknown> = Record<string, never>,
> extends MySqlDatabase<MySqlRemoteQueryResultHKT, MySqlRemotePreparedQueryHKT, TSchema> {
}
// @filename: drizzle-orm/src/mysql-proxy/migrator.ts
import { sql } from '~/sql/sql.ts';
import type { MySqlRemoteDatabase } from './driver.ts';
export async function migrate<TSchema extends Record<string, unknown>>(
	db: MySqlRemoteDatabase<TSchema>,
) {
	const migrationTableCreate = sql`
	`;
	const dbMigrations = await db.select({
	}).from(sql.identifier(migrationsTable).getSQL()).orderBy(
	).limit(1);
}
// @filename: drizzle-orm/src/query-builders/select.types.ts
import type { Table } from '~/table.ts';
export type ApplyNullabilityToColumn<TColumn extends Column, TNullability extends JoinNullability> =
	TNullability extends 'not-null' ? TColumn
		: Column<
		>;
export type AddAliasToSelection<
> = TOldSelectMode extends 'partial' ? TResult
	: TResult & (TJoinedName extends string ? Record<TJoinedName, TSelectedFields> : TSelectedFields);
export type BuildSubquerySelection<
	TSelection extends ColumnsSelection,
	TNullability extends Record<string, JoinNullability>,
> = TSelection extends never ? any
	:
		& {};
export type AppendToNullabilityMap<
> = TJoinedName extends string ? 'left' extends TJoinType ? TJoinsNotNull & { [name in TJoinedName]: 'nullable' }
	: never
	: TJoinsNotNull;
export type GetSelectTableName<TTable extends TableLike> = TTable extends Table ? TTable['_']['name']
	: TTable extends SQL ? undefined
	: never;
// @filename: drizzle-orm/src/sql/sql.ts
export class SQL<T = unknown> implements SQLWrapper {
	mapWith<
	>(decoder: TDecoder): SQL<GetDecoderResult<TDecoder>> {
	}
}
export type GetDecoderResult<T> = T extends Column ? T['_']['data'] : T extends
	| DriverValueDecoder<infer TData, any>['mapFromDriverValue'] ? TData
: never;
export class Name implements SQLWrapper {
	getSQL(): SQL<unknown> {
	}
}
export namespace sql {
	/**
	 */
	export function identifier(value: string): Name {
	}
}
export abstract class View<
> implements SQLWrapper {
	declare readonly $inferSelect: InferSelectViewModel<View<Assume<TName, string>, TExisting, TSelection>>;
}
// @filename: drizzle-orm/src/table.ts
export interface Table<
> extends SQLWrapper {
	[ExtraConfigBuilder]: ((self: any) => Record<string, unknown> | unknown[]) | undefined = undefined;
}
