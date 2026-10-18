// A class's own members at the same distance: the first declared wins (tsgo's compareSymbols by declaration). The
// instance type's members are instantiated for `this`: their declarations are their targets'
class Manager {
    startExtension = 1;
    async restartExtension() {}
    f() { this.maybeStartExtension; }
}
class Generic<T> {
    truncateTableQuery(t: T) {}
    _replaceTableQuery(t: T) {}
}
declare const g: Generic<string>;
g.createTableQuery;
