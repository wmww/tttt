// @strict: true
// @target: es2022
// sequelize's connection-options.ts: destructuring a generic union narrows the pattern by its constraint
// (tsc's getNarrowedTypeOfSymbol), whose element lookup reports TS2339 again on the constraint
declare const OT: unique symbol;
declare class AD<O extends object = object> { [OT]: O; parse(url: string): O; }
type CO<T extends AD> = T[typeof OT];
type Raw<T extends AD> = (CO<T> & { url?: string }) | string;
function f<T extends AD>(dialect: T, options: Raw<T>): CO<T> {
  const { url, ...rest } = options;
  if (url) { return dialect.parse(url); }
  return rest;
}
