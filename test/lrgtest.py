"""

Line RanGe (LRG) -- Python script to run automated tests for lrg
Copyright (c) 2017-2021 Sampo Hippeläinen (hisahi)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""

import os.path
import os
import subprocess
import random
import math
import sys
import traceback

try:
    import colorama
    colorama.init()
    color = True
except:
    color = False

MAX_LINES = 10000


verbosity = 0


def fuzz(n):
    if fuzz.mul is None:
        j = random.randint(1, MAX_LINES * 99 // 100)
        while math.gcd(MAX_LINES, j) != 1:
            j = (j + 1) % MAX_LINES
        fuzz.mul = j
    return hex(hash(str((n * fuzz.mul) % MAX_LINES) + fuzz.hash) & 0xFFFFFFFFFFFFFFFF)[2:]


fuzz.mul = None
fuzz.hash = "".join(random.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
                    for i in range(6))


def mapColor(color):
    return {
        "black": colorama.Fore.BLACK,
        "gray": colorama.Style.BRIGHT + colorama.Fore.BLACK,
        "maroon": colorama.Fore.RED,
        "red": colorama.Style.BRIGHT + colorama.Fore.RED,
        "green": colorama.Fore.GREEN,
        "lime": colorama.Style.BRIGHT + colorama.Fore.GREEN,
        "orange": colorama.Fore.YELLOW,
        "yellow": colorama.Style.BRIGHT + colorama.Fore.YELLOW,
        "blue": colorama.Fore.BLUE,
        "skyblue": colorama.Style.BRIGHT + colorama.Fore.BLUE,
        "purple": colorama.Fore.MAGENTA,
        "magenta": colorama.Style.BRIGHT + colorama.Fore.MAGENTA,
        "cyan": colorama.Fore.CYAN,
        "turquoise": colorama.Style.BRIGHT + colorama.Fore.CYAN,
        "silver": colorama.Fore.WHITE,
        "white": colorama.Style.BRIGHT + colorama.Fore.WHITE,
        "reset": colorama.Style.RESET_ALL
    }.get(color, "")


def colorPrint(color, *a, **kw):
    if color:
        modified = list(a)
        modified[0] = "".join(mapColor(c) for c in color.split()) + modified[0]
        modified[-1] += mapColor("reset")
        print(*modified, **kw)
    else:
        print(*a, **kw)


def makeExpectedLrgOutput(s, pipe):
    q = []
    expect_error = False
    for t in s.split(","):
        if "~" in t:
            a, b = t.split("~")
            if not b:
                b = "3"
            try:
                a, b = int(a), int(b)
            except ValueError:
                return ([], True)
            assert b >= 0
            a, b = a - b, a + b
            if b > MAX_LINES:
                expect_error = True
                b = MAX_LINES
            if a < 1:
                a = 1
            if pipe and q and a <= max(q):
                expect_error = True
                break
            q += list(range(a, b + 1))
        elif "-" in t:
            a, b = t.split("-")
            if not b:
                b = str(MAX_LINES)
            try:
                a, b = int(a), int(b)
            except ValueError:
                return ([], True)
            if b > MAX_LINES:
                expect_error = True
                b = MAX_LINES
            if a < 1:
                return ([], True)
            if pipe and q and a <= max(q):
                expect_error = True
                break
            q += list(range(a, b + 1))
        else:
            try:
                a = int(t)
            except ValueError:
                return ([], True)
            if a < 1:
                return ([], True)
            elif a > MAX_LINES:
                expect_error = True
                a = MAX_LINES
            if pipe and q and a <= max(q):
                expect_error = True
                break
            q.append(a)
    return [fuzz(x) for x in q], expect_error


def convertLrgOutput(x):
    return [s.strip() for s in x.strip().splitlines() if s]


class TestProgram():
    def __init__(self, name, flags, fname, pipe):
        self.name = name
        self.flags = flags
        self.fname = fname
        self.pipe = pipe

    def run(self, ranges):
        proc = [self.name] + self.flags + [ranges]
        if self.pipe:
            if verbosity >= 1:
                print(proc)
            # must not be seekable!
            with open(self.fname, "rb") as f_in:
                stdout, stderr = subprocess.Popen(
                    proc, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE).communicate(f_in.read())
            stdout, stderr = stdout.decode('ascii'), stderr.decode('ascii')
        else:
            proc.append(self.fname)
            if verbosity >= 1:
                print(proc)
            result = subprocess.run(
                proc, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = result.stdout.decode(
                'ascii'), result.stderr.decode('ascii')
        return convertLrgOutput(stdout), bool(stderr.strip())


class TestCase():
    def __init__(self, ranges, description=None, text=None):
        self.text = text or ranges
        self.ranges = ranges
        self.description = description
        self.expected = makeExpectedLrgOutput(self.ranges, False)
        self.lastResult = None

    def run(self, program):
        self.lastResult = program.run(self.ranges)
        if verbosity >= 2:
            print(self.expected, self.lastResult)
        return self.expected == self.lastResult


class TestCaseFailOnPipe(TestCase):
    def run(self, program):
        if program.pipe:
            self.expected = makeExpectedLrgOutput(self.ranges, True)
        return super().run(program)


def printTestSetHeader(header):
    colorPrint("turquoise", " " + header)
    colorPrint("gray", "=" * (len(header) + 2))
    print("")


def printTestGroupHeader(header):
    colorPrint("yellow", " " + header)
    colorPrint("gray", "=" * (len(header) + 2))


def printTableSep(widths):
    colorPrint("gray", ("-+-".join("-" * w for w in [0] + widths + [0]))[1:-1])


def printTableHeaders(widths, headers):
    colorPrint("gray", "| ", end="")
    for w, h in zip(widths, headers):
        colorPrint("cyan", h.ljust(w), end="")
        colorPrint("gray", " | ", end="")
    print("")


def printTableTestCase(widths, name, ok):
    colorPrint("gray", "| ", end="")
    print(name.ljust(widths[0]), end="")
    colorPrint("gray", " | ", end="")
    if ok:
        colorPrint("green", "OK".ljust(widths[1]), end="")
    else:
        colorPrint("red", "FAIL".ljust(widths[1]), end="")
    colorPrint("gray", " |")


class TestGroup():
    def __init__(self, header, cases):
        self.header = header
        self.cases = cases

    def fail(self, program, case):
        colorPrint("orange", "Test failed!")
        if case.description:
            colorPrint("cyan", "Test notes: ", end="")
            colorPrint("white", case.description)
        colorPrint("maroon", "Return value: ", end="")
        print(case.lastResult)
        colorPrint("green", "Expected: ", end="")
        print(case.expected)
        return False

    def exception(self, program, e):
        colorPrint("red", "There was an error while running the test!")
        traceback.print_exception(type(e), e, e.__traceback__)
        return False

    def run(self, program):
        headers = ["Test Case", "Result"]
        tableWidths = [max(len(c.text) for c in self.cases), 4]
        tableWidths = [max(w, len(h)) for w, h in zip(tableWidths, headers)]
        printTestGroupHeader(self.header)
        printTableSep(tableWidths)
        printTableHeaders(tableWidths, headers)
        printTableSep(tableWidths)
        for c in self.cases:
            try:
                success = c.run(program)
            except Exception as e:
                printTableTestCase(tableWidths, c.text, False)
                printTableSep(tableWidths)
                return self.exception(program, e)
            printTableTestCase(tableWidths, c.text, success)
            if not success:
                printTableSep(tableWidths)
                return self.fail(program, c)
        printTableSep(tableWidths)
        print("")
        return True


testGroups = [TestGroup(
    "Basic single-line tests",
    [
        TestCase("1"),
        TestCase("3"),
        TestCase("8"),
        TestCase("8000")
    ]
), TestGroup(
    "Range tests",
    [
        TestCase("1-5"),
        TestCase("100-101"),
        TestCase("2-200"),
        TestCase("100-90", "should run, but be empty (no lines printed)"),
        TestCase("{}-{}".format(MAX_LINES - 2, MAX_LINES),
                 "should not warn about EOF"),
        TestCase("{}-{}".format(MAX_LINES - 2, MAX_LINES + 1),
                 "should warn about EOF"),
        TestCase("{}-".format(MAX_LINES - 10), "should not warn about EOF"),
    ]
), TestGroup(
    "Lookaround range tests",
    [
        TestCase("10~3"),
        TestCase("2~5"),
        TestCase("{}~3".format(MAX_LINES - 3), "should not warn about EOF"),
        TestCase("{}~4".format(MAX_LINES - 3), "should warn about EOF"),
    ]
), TestGroup(
    "Ad-hoc tests",
    [
        TestCase("7-17")
    ]
), TestGroup(
    "Error tests",
    [
        TestCase("0", "should cause an invalid range error"),
        TestCase("0-7", "should cause an invalid range error"),
        TestCase("a", "should cause an invalid range error"),
        TestCase("3-b", "should cause an invalid range error"),
        TestCase("b-5", "should cause an invalid range error"),
        TestCase("-", "should cause an invalid range error"),
    ]
), TestGroup(
    "Seeking backwards",
    [
        TestCaseFailOnPipe("2,1"),
        TestCaseFailOnPipe("6000,4000"),
        TestCaseFailOnPipe("6000,2000"),
        TestCaseFailOnPipe("9000,1000"),
        TestCaseFailOnPipe("9001,1520"),
        TestCaseFailOnPipe("9002,2222"),
        TestCaseFailOnPipe("9003,2222,4444,6666,8888"),
        TestCaseFailOnPipe("9004,2222,4444,6666,8888,4444,6666,2222"),
        TestCaseFailOnPipe("1,1"),
        TestCaseFailOnPipe("7538,3239,708,8325,8325,5450,1326,7203,3237,1326"),
    ]
), TestGroup(
    "Random single-line tests",
    [
        TestCase(",".join(str(random.randint(1, MAX_LINES)
                 for i in range(20))), text="Batch {}/5".format(j + 1))
        for j in range(5)
    ]
)]
assert MAX_LINES >= 10000


def createFile():
    n = 1
    while True:
        f = "tmp-{}.txt".format(n)
        if not os.path.exists(f):
            break
        n += 1
    with open(f, "w", encoding="ascii") as ff:
        for n in range(MAX_LINES):
            print(fuzz(n + 1), file=ff)
    return f


def deleteFile(name):
    os.remove(name)


def runTests(argv):
    global verbosity
    BINARY = "lrg"
    verbosity = argv.count("-v")
    noflags = False
    for v in argv[1:]:
        if noflags or not v.startswith("-"):
            BINARY = v
            break
        elif v == "--":
            noflags = True
    tmp = createFile()
    try:
        printTestSetHeader("File mode")
        p = TestProgram(BINARY, ["-w"], tmp, False)
        for g in testGroups:
            if not g.run(p):
                return 1
        printTestSetHeader("Pipe mode")
        try:
            r, w = os.pipe()
            os.close(w)
            os.close(r)
            pipe = True
        except OSError:
            pipe = False
            traceback.print_exc()
        if pipe:
            p = TestProgram(BINARY, ["-w"], tmp, True)
            for g in testGroups:
                if not g.run(p):
                    return 1
        else:
            colorPrint("yellow", "WARNING: pipes not supported, cannot test")
        colorPrint("lime", "All tests successful!")
    finally:
        deleteFile(tmp)
    return 0


if __name__ == "__main__":
    sys.exit(runTests(sys.argv))
