// origin: npm Bitterbot-AI/bitterbot-desktop@main https://github.com/Bitterbot-AI/bitterbot-desktop tsconfig.json seed 20261401 (commit 733b74195)
// flip: yes
// Cut by hand from grammy 1.46's frameworks.d.ts (`HttpAdapter = (req: { on: (...) => typeof req; ... }) => ...`) and
// src/telegram/bot.ts's `createTelegramWebhookCallback` (`webhookCallback(bot, "http")`, whose type spreads
// `Parameters<ResolveName<A>>`): the parameter `req` isn't accessible where the declaration is printed, so `typeof req`
// expands to the object type it is part of, a cyclic structure: TS5088 (and `any` in the types). tttt took a
// parameter of a function in another file for accessible and reused the query.
// @declaration: true
// @strict: true
// @filename: lib.d.ts
export type HttpAdapter = (req: {
    on: (event: string) => typeof req;
}) => void;
export declare function cb<A extends HttpAdapter>(a: A): (...args: Parameters<A>) => void;
// @filename: main.ts
import { cb, type HttpAdapter } from "./lib";
declare const h: HttpAdapter;
export function f() {
  return { handler: cb(h) };
}
export function g() { return h; }
type P = Parameters<HttpAdapter>;
declare const p: P;
export function k() { return p; }
