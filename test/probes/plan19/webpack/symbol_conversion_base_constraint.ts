// @strict: true
function f<T extends object>(obj: T) {
    for (const key_ of Object.keys(obj)) {
        const key = key_ as keyof T;
        if (typeof key === "string" && key.startsWith("by")) {
            const s = `${key}`;
            const g = () => `${key}`;
        }
    }
}
