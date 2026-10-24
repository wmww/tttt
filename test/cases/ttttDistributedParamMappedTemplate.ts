// @strict: true
// Instantiating a distributed type parameter through a mapper that doesn't map it gives its original (tsgo's
// getMappedType maps getNonDistributedTypeParameter(t)): two conditional types over V written twice have generic mapped
// false branches whose templates, instantiated for the key, compare V with the distributed V, unrelated (TS2719, with
// TS5113's elaboration and no TS2208 for the original V: its synthetic constraint would be circular)
function h1<K extends string, V>(x: V extends undefined ? 1 : { [P in K]: V }) { const y: V extends undefined ? 1 : { [P in K]: V } = x; }
function h2<K extends string, V>(x: V extends undefined ? 1 : { [P in K]: 2 }) { const y: V extends undefined ? 1 : { [P in K]: 2 } = x; }
function h3<K extends string, V>(x: V extends undefined ? 1 : { [P in "a"]: V }) { const y: V extends undefined ? 1 : { [P in "a"]: V } = x; }
function h4<K extends string, V>(x: V extends 1 ? 1 : { [P in K]: V }) { const y: V extends 1 ? 1 : { [P in K]: V } = x; }
function h5<K extends string, V>(x: V extends undefined ? { [P in K]: V } : 1) { const y: V extends undefined ? { [P in K]: V } : 1 = x; }
