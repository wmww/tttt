// `NoInfer<T>` counts its alias name towards the truncation length (tsc's symbolToTypeNode; mikro-orm's TS1270)
// @noErrorTruncation: false
// @strict: true
// @target: es2022
type EntityType = object;
type EntityName<T> = string | (new () => T);
type FilterQuery<T> = Partial<T>;
interface FindOneOptions<E, H extends string = never, F extends string = "*", X extends string = never> { e?: E; h?: H; f?: F; x?: X }
declare function Transactional(): MethodDecorator;
declare const em: { findOne(a: any, b: any, c: any): Promise<number> };
export class Repo {
  @Transactional()
  async findOne<
    Entity extends EntityType,
    Hint extends string = never,
    Fields extends string = '*',
    Excludes extends string = never,
  >(
    entityName: EntityName<Entity>,
    where: FilterQuery<NoInfer<Entity>>,
    options?: FindOneOptions<Entity, Hint, Fields, Excludes>,
  ) {
    return em.findOne(entityName, where as any, options);
  }
}
