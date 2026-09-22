// @strict: true
// @target: es2020
interface Host {
    getSourceFile(fileName: string, version: number, onError?: (m: string) => void): string | undefined;
    writeFile?(fileName: string, data: string, bom: boolean): void;
}
declare const host: Host;
const orig = host.getSourceFile;
host.getSourceFile = (...args) => orig.call(host, ...args);
host.getSourceFile = (fileName, ...args) => orig(fileName, ...args);
host.writeFile = (fileName, data, ...rest) => { rest; };
