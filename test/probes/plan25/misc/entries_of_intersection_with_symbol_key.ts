// @strict: true
// @target: es2017
// @mswjs/data (testing/mocks/db.ts): Object.entries(db).forEach(([key, model]) => ...) where
// db: FactoryAPI<D> = { [K in keyof D]: ModelAPI<D, K> } & { [DATABASE_INSTANCE]: Database }
// Implicit index signature inference (tsc getImplicitIndexTypeOfType via isObjectTypeWithInferableIndex):
// works for intersections of inferable object types, and only counts properties whose key fits the index key
declare const DATABASE_INSTANCE: unique symbol;
interface ModelAPI<K> { create(x: object): K; }
type FactoryAPI<D> = { [K in keyof D]: ModelAPI<K> } & { [DATABASE_INSTANCE]: number };
declare const db: FactoryAPI<{ user: 1; team: 2 }>;
Object.entries(db).forEach(([key, model]) => {
    model.create({});
});

declare function entries<T>(o: { [s: string]: T }): T;
declare const S: unique symbol;
declare const a: { x: 1 } & { y: 2 };
const ea = entries(a); // 1 | 2
declare const c: { x: 1; [S]: 2 };
const ec = entries(c); // 1, the symbol-keyed property doesn't count
