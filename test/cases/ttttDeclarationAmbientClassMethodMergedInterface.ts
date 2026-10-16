// @declaration: true
// @target: es2022
// A class method an interface merged with the class declares too is left out only when it has a body (tsc's
// isImplementationOfOverload): an ambient class's stays (vscode's declare class FileSystemObserver)
export interface Obs {
	observe(handle: string): Promise<void>;
	observe(handle: number, options?: { recursive: boolean }): Promise<void>;
}
export declare class Obs {
	constructor(callback: () => void);
	observe(handle: string, options?: { recursive: boolean }): Promise<void>;
}
export interface Impl {
	run(a: string): void;
}
export class Impl {
	run(a: string | number): void {}
}
