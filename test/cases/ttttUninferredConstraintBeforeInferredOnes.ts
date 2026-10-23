// @strict: true
// @noEmit: true
// A type parameter without inferences or a default is its constraint (here an unresolved import's, so any) before
// a later inferred one's constraint, which mentions it, is checked: K keeps "Comment" rather than becoming
// `keyof unknown['P']`, never (pothos's prismaConnectionHelpers with @pothos/core unbuilt)
// @filename: helpers.ts
import type { SchemaTypes } from 'nonexistent-module';
interface Builder<T> { b: T }
export declare function pick<Types extends SchemaTypes, K extends keyof Types['P'], Model = Types['P'][K]>(b: Builder<Types>, key: K): K;
interface Ref<T, M> { t: T; m: M }
interface ModelTypes { Select: unknown; Shape: unknown }
export declare function helpers<
  Types extends SchemaTypes,
  R extends Ref<Types, ModelTypes> | keyof Types['P'],
  Select extends Model['Select'] & {},
  Model extends ModelTypes = R extends Ref<Types, infer X> ? ModelTypes & X : ModelTypes & Types['P'][R & keyof Types['P']],
>(b: Builder<Types>, r: R): R;

// @filename: use.ts
import { pick, helpers } from './helpers';
declare const builder: any;
const a = pick(builder, 'Comment');
const b = helpers(builder, 'Comment');
