// @strict: true
type FileId = number & { __fileIdBrand: any };
type Root = [fileId: FileId, resolved: FileId];
declare function append<T>(to: T[] | undefined, value: T | undefined): T[];
declare function toFileId(s: string): FileId;
let result: Root[] | undefined;
result = append(result, [toFileId("a"), toFileId("b")]);
