//// [test/cases/ttttUnresolvedRestPosition.ts] ////

//// [ttttUnresolvedRestPosition.ts]
// A rest parameter of an unresolved type gives each position that type (tsc's getIndexedAccessType of an any is the
// any itself): `execute(client)` contextually typed by `(...p: Missing["x"])` is `client: Missing`
import type { ClientEvents } from 'nonexistent-module';
type Ev = { execute(...parameters: ClientEvents["x"]): void };
export const a = { execute(client) {} } satisfies Ev;
export const b: Ev = { execute(client) {} };
export const c = { execute(client, d) {} } satisfies Ev;
type Ev2 = { execute(p: ClientEvents["x"]): void };
export const e = { execute(client) {} } satisfies Ev2;
type Ev3 = { execute(...p: ClientEvents): void };
export const g = { execute(client) {} } satisfies Ev3;


//// [ttttUnresolvedRestPosition.js]
export const a = { execute(client) { } };
export const b = { execute(client) { } };
export const c = { execute(client, d) { } };
export const e = { execute(client) { } };
export const g = { execute(client) { } };


//// [ttttUnresolvedRestPosition.d.ts]
import type { ClientEvents } from 'nonexistent-module';
type Ev = {
    execute(...parameters: ClientEvents["x"]): void;
};
export declare const a: {
    execute(client: ClientEvents): void;
};
export declare const b: Ev;
export declare const c: {
    execute(client: ClientEvents, d: ClientEvents): void;
};
export declare const e: {
    execute(client: ClientEvents): void;
};
export declare const g: {
    execute(client: ClientEvents): void;
};
export {};
