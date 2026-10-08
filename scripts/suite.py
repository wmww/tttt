"""Where the baseline test suite lives (tsgo's native tree, plan 54): the scripts import this instead of hardcoding it.

  TESTS     the tests directory (cases/, lib/), what baseline_tests' --dir takes
  CASES     TESTS/cases (compiler/, conformance/; transpile/ isn't a baseline suite)
  ref(name, ext)   tsc's baseline of a test: ref('foo(target=es2015)', '.types'). Baselines are per suite
                   (reference/compiler/, reference/conformance/), found from the test file's directory
  test_files()     every test file (path), compiler and conformance
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTDATA = os.path.join(ROOT, 'thirdparty/typescript/tsc/testdata')
TESTS = os.path.join(TESTDATA, 'tests')
CASES = os.path.join(TESTS, 'cases')
REF_ROOT = os.path.join(TESTDATA, 'baselines/reference')
SUITES = ('compiler', 'conformance')

_kinds = None


def test_files():
    for suite in SUITES:
        for root, _, files in os.walk(os.path.join(CASES, suite)):
            for f in files:
                if f.endswith('.ts') or f.endswith('.tsx'):
                    yield os.path.join(root, f)


def test_kind(name):
    """'compiler' or 'conformance' for a test name (with or without its configuration)"""
    global _kinds
    if _kinds is None:
        _kinds = {}
        for path in test_files():
            rel = os.path.relpath(path, CASES)
            _kinds[os.path.basename(path).rsplit('.', 1)[0]] = rel.split(os.sep)[0]
    return _kinds.get(name.split('(')[0], 'compiler')


def ref(name, ext=''):
    return os.path.join(REF_ROOT, test_kind(name), name + ext)
