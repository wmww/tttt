// @strict: true
declare function dc<Props extends Record<string, any>>(setup: (props: Props) => () => void, options?: { props?: (keyof NoInfer<Props>)[] }): new (props: Props) => { $props: Props }
declare function dc<Props extends Record<string, any>>(setup: (props: Props) => () => void, options?: { props?: { [K in keyof Props]: unknown } }): new (props: Props) => { $props: Props }
dc(<T extends string>(_props: { msg: T }) => () => {}, { props: { bar: String } })
const ok = dc(<T extends string>(_props: { msg: T }) => () => {}, { props: ['msg'] })
