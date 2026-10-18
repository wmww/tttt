// TS7053 prints the indexed object's apparent type: an intersection made again from its members, without the alias
// that named it (tsc's getApparentTypeOfIntersectionType; mikro-orm's Loaded<User>)
// @strict: true
declare const __loadedType: unique symbol;
type LoadedInternal<T, L extends string> = [L] extends [never] ? T & {} : T & { l: L };
type Loaded<T, L extends string = never> = LoadedInternal<T, L> & { [__loadedType]?: T };
class User { name!: string }
declare function find<E extends object, H extends string = never>(e: new () => E): Promise<Loaded<E, H>>;
declare const k: any;
async function f() {
  const user = await find(User);
  user[k];
  const u2 = null! as Loaded<User>;
  u2[k];
}
