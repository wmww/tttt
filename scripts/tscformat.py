"""tsc's `--pretty false` diagnostics, what tttt prints too: `file(line,col): error TSxxxx: message`, a diagnostic
without a file as `error TSxxxx: message`, the lines of its message chain indented under it"""
import re

DIAG = re.compile(r'^(?:(\S.*?)\((\d+),(\d+)\): )?(error|warning|suggestion|message) TS(\d+): (.*)$')


def parse(text):
    """(file, line, column, code, head message) of each diagnostic, ('', 0, 0, ...) without a file"""
    ret = []
    for line in text.split('\n'):
        m = DIAG.match(line)
        if m:
            ret.append((m.group(1) or '', int(m.group(2) or 0), int(m.group(3) or 0), int(m.group(5)), m.group(6)))
    return ret


def blocks(text):
    """Each diagnostic's whole text: its header line and the chain lines under it"""
    ret = []
    for line in text.split('\n'):
        if DIAG.match(line):
            ret.append(line)
        elif ret and line.startswith('  '):
            ret[-1] += '\n' + line
    return ret
