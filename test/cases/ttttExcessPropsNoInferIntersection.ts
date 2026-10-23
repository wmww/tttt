// @strict: true
// An intersection target with a NoInfer member is not a generic object type (tsc's isGenericObjectType reads a
// substitution type's base and constraint), so its combined properties are related too: the nested object literal's
// excess property is reported. better-drizzle's `findMany<Args extends Q>(args?: Args & NoInfer<Q & ...>)`, whose
// Args falls back to its constraint when the literal fails it
type F = { a?: 'asc' | 'desc' };
type Q = { orderBy?: F | F[] };
type Q2 = { orderBy?: F };
declare function find<A extends Q>(args?: A & NoInfer<Q>): void;
find({ orderBy: { writer: { name: 'asc' } } });
find({ orderBy: { a: 'asc', writer: 1 } });
const s1: Q & NoInfer<Q> = { orderBy: { writer: 1 } };
const s2: Q & NoInfer<Q2> = { orderBy: { writer: 1 } };
const s3: Q & Q2 = { orderBy: { writer: 1 } };
