// @strict: true
// @lib: es2015
interface Opts { name?: string; x?: number }
interface FC { (props: {}): any; displayName?: string }
type Def = (Opts | FC) & { extra?: boolean }
declare const def: Def
def.name = 'V'
declare const def2: Opts | FC
def2.name = 'V'
declare const f: FC
f.name = 'V'
