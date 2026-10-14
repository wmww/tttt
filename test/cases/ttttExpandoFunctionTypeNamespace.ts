// @noEmit: true
// A function with expandos and a merged namespace that isn't instantiated prints structurally (tsc's createAnonymousTypeNode wants SymbolFlags.ValueModule for typeof): nodemailer's nmfetch
function nmfetch(url: string): number { return 1; }
nmfetch.DEFAULT_TIMEOUT = 1;
declare namespace nmfetch { export type O = string; }
nmfetch.Nope;
function g(url: string): number { return 1; }
declare namespace g { export type O = string; }
g.Nope;
function h(url: string): number { return 1; }
namespace h { export const x = 1; }
h.Nope;
export default nmfetch;
