// @strict: true
type DeepPartial<T> = {
  [P in keyof T]?: DeepPartial<T[P]>;
};
interface User { id: number | string; email: string | null; provider: string; socialId?: string | null; }
declare function update(p: DeepPartial<User>): void;
declare const e: string | null | undefined;
update({ email: e });
declare const p: string | undefined;
update({ provider: p, socialId: e });
type X = DeepPartial<User>["email"];
type Y = DeepPartial<string | null>;
const y: Y = e;
