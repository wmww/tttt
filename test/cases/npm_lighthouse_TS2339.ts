// origin: npm tybrs-lighthouse@1.0.0 https://github.com/googlechrome/lighthouse tsconfig.bench.json seed 20261217, core/lib/sentry.js:131
// flip: no
// A parameter's `{}` default is no expando container (tsc's getInitializerSymbol takes variables only): a contextually
// typed `opts = {}` keeps its contextual type, and `o.z = 1` on an untyped one is TS2339
// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
const d = {
  /** @type {(error: Error, options: {level?: string, tags?: {[key: string]: any}, extra?: {[key: string]: any}}) => Promise<void>} */
  captureException: async () => { },
};
d.captureException = async (err, opts = {}) => {
  const tags = opts.tags || {};
  if (tags.audit) {}
  opts.fingerprint = ['x'];
  opts.tags = opts.tags || {};
  opts.tags.p = 1;
  if (opts.level) {}
  if (opts.extra) {}
};
/** @param {{a?: number}} o */
function f(o = {}) {
  o.b = 1;
  return o.a;
}
const g = (o = {}) => { o.z = 1; return o.z; };
