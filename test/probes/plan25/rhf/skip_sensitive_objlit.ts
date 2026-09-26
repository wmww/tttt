// First inference pass must check an object literal argument with context-sensitive members, skipping only those
// members (tsc CheckMode.SkipContextSensitive), so `s: 1` infers S before `cb` fixes it. tsc: all number
declare function g4<S>(p: { s: S; cb: (v: S) => void }): S;
const b4 = g4({ s: 1, cb: (v) => {} });
const b5 = g4({ s: 1, cb: v => {} });
const b6 = g4({ s: 1, cb: function(v) {} });
const b7 = g4({ s: 1, cb(v) {} });
const b8 = g4({ cb: (v) => {}, s: 1 });
declare function g5<S>(s: S, cb: (v: S) => void): S;
const c1 = g5(1, (v) => {});
declare function g6<S>(p: { s: S; cb: (v: S) => void }, q?: 1): S;
const c2 = g6({ s: 1, cb: (v) => {} });
