// @strict: true
type StateSchema = { id?: string; states?: Record<string, StateSchema>; type?: unknown };
type ToStateSchema<TSchema extends StateSchema> = {
  -readonly [K in keyof TSchema as K & ('id' | 'states')]: K extends 'states'
    ? { [SK in keyof TSchema['states']]: ToStateSchema<NonNullable<TSchema['states'][SK]>> }
    : TSchema[K];
};
interface NodeConfig { id?: string; states?: { [K in string]: NodeConfig }; type?: 'atomic' }
type Want<S extends StateSchema> = S
type Cfg<TContext> = NodeConfig & (object extends TContext ? { context?: TContext } : { context: TContext })
function f<TContext, const TConfig extends Cfg<TContext>>(c: TConfig) {
  type R = Want<ToStateSchema<TConfig>>
  const x: TConfig["states"] = null!
}
