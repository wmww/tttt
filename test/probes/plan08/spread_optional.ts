// @strict: true
interface Levels { Low: number; Medium: number; High: number }
declare function use(l: Levels): void;
declare const def: Levels;
declare const custom: { Low?: number; Medium?: number; High?: number } | undefined;
use(custom ? { ...def, ...custom } : def);
const s = { ...def, ...custom! };
