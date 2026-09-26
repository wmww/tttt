// @strict: true
// @ngneat/falso: randUserName({ withAccents: false }) returns Return<string, { withAccents: false }>, which tsc
// resolves to string
interface FakeOptions { length?: number; locale?: any | string[]; }
interface UserNameOptions extends FakeOptions { withAccents?: boolean; }
type Return<T, O extends FakeOptions> = [O] extends [never] ? T : O['length'] extends number ? T[] : T;
declare function randUserName<Options extends UserNameOptions = never>(options?: Options): Return<string, Options>;
const name = randUserName({ withAccents: false });
const s: string = name;
const none = randUserName();
type R1 = Return<string, { withAccents: false }>;
// The mechanism: an indexed access instantiated to a missing property is unknown (tsc getIndexedAccessType
// without an access node), so `O['length'] extends number` is false
type G<O extends { x: 1; length?: number }> = O['length'];
type L = G<{ x: 1; withAccents: false }>;
declare const l: L;
