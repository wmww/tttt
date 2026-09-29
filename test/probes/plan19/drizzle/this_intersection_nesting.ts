// Must check instantly with no errors (tsc: 0.5 s): variance measurement of ColumnBuilder relates methods returning
// `this & {...}`, each level a new intersection containing ColumnBuilder<sub-T>; tsc sees them as deeply nested
// through their members (isDeeplyNestedType on intersections). Before: 29 s, TS2589, 1.2M types
interface Config { name: string; data: unknown; notNull: boolean; hasDefault: boolean; $type?: unknown }
type NotNull<T extends ColumnBuilderBase> = T & { _: { notNull: true } };
type HasDefault<T extends ColumnBuilderBase> = T & { _: { hasDefault: true } };
type IsPrimaryKey<T extends ColumnBuilderBase> = T & { _: { isPrimaryKey: true } };
type $Type<T extends ColumnBuilderBase, TType> = T & { _: { $type: TType } };
interface ColumnBuilderBase<T extends Config = Config, TTypeConfig extends object = object> { _: T & TTypeConfig }
interface ExtraConfig { primaryKeyHasDefault?: boolean }
abstract class ColumnBuilder<T extends Config = Config, TRuntimeConfig extends object = object, TTypeConfig extends object = object, TExtraConfig extends ExtraConfig = ExtraConfig>
    implements ColumnBuilderBase<T, TTypeConfig> {
    declare _: T & TTypeConfig;
    $type<TType>(): $Type<this, TType> { return this as any; }
    notNull(): NotNull<this> { return this as any; }
    $onUpdateFn(fn: () => (this['_'] extends { $type: infer U } ? U : this['_']['data'])): HasDefault<this> { return this as any; }
    primaryKey(): TExtraConfig['primaryKeyHasDefault'] extends true ? IsPrimaryKey<HasDefault<NotNull<this>>> : IsPrimaryKey<NotNull<this>> { return this as any; }
}
abstract class PgColumnBuilder<T extends Config = Config, TRuntimeConfig extends object = object, TTypeConfig extends object = object, TExtraConfig extends ExtraConfig = ExtraConfig>
    extends ColumnBuilder<T, TRuntimeConfig, TTypeConfig & { dialect: 'pg' }, TExtraConfig> {
}