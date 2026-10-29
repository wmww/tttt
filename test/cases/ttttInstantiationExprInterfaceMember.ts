// A namespace member access in an instantiation expression or extends clause naming an interface is TS2689 at the
// access, not TS2339 (bun case 06-C16, tsc's checkAndReportErrorForExtendingInterface from the property access)
// @strict: true
namespace NI { export interface Inner<T> { } export const v = 1; export namespace Deep { export interface I {} } }
interface Top<T> {}
const x1 = NI.Inner<string>;
const x2 = NI.Deep.I<string>;
const x3 = Top<string>;
class C extends NI.Inner<number> {}
class D extends NI.Deep.I {}
const x4 = NI.Missing<string>;
const x5 = (NI.Inner)<string>;
