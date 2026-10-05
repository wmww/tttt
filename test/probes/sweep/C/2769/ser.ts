interface Dictionary<T> { [key: string]: T }
type Fn<T> = (cb: (err: Error | undefined, r?: T) => void) => void;
declare function series<T>(tasks: Fn<T>[], callback: (err: Error | undefined, results: T[]) => void): void;
declare function series<T>(tasks: Dictionary<Fn<T>>, callback: (err: Error | undefined, results: Dictionary<T>) => void): void;
series({ one: cb => { cb(undefined, 1); } }, (err, results) => {});
