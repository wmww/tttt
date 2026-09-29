// A method returning `this` on an intersection whose member is an alias of a class reference (a deferred reference)
interface Base { _: { notNull: boolean } }
type NotNull<T extends Base> = T & { _: { notNull: true } };
declare class Builder<T> implements Base {
    _: { notNull: boolean; data: T };
    notNull(): NotNull<this>;
    unique(): this;
}
type Initial<N extends string> = Builder<{ name: N }>;
declare const b: Initial<"a">;
const r = b.notNull().unique();
type R = typeof r["_"]["notNull"];
