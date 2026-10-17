"""Run unchanged with CPython or the real MicroPython static ELF."""
import gc
import sys
import os
import math
import json
import re
import struct
from array import array
from terrain import chunk

class Block:
    def __init__(self, value):
        self.value = value

class Grass(Block):
    def score(self, scale):
        return self.value * scale

blocks = chunk(21)
checksum = sum(sum(row) for row in blocks)
assert len(blocks) == 16 and all(len(row) == 16 for row in blocks)
assert checksum == 8064 and Grass(7).score(3) == 21
assert str(2 ** 80) == "1208925819614629174706176"
assert list(x * x for x in range(5)) == [0, 1, 4, 9, 16]
assert [x for x in range(8) if x % 2] == [1, 3, 5, 7]
assert math.sqrt(81) == 9.0 and abs(math.sin(0.5) - 0.4794255386) < 0.000001
assert json.loads(json.dumps({"chunk": checksum}))["chunk"] == checksum
assert re.match("hello.*world", "hello world")
assert struct.unpack("<I", struct.pack("<I", checksum))[0] == checksum
assert list(array("H", [1, 21, 8064])) == [1, 21, 8064]
assert "你好"[1:] == "好"
try:
    raise ValueError("caught Python error")
except ValueError as error:
    assert "caught Python error" in str(error)
else:
    raise AssertionError("exception was lost")
with open("python-world.txt", "w") as stream:
    stream.write("Python persistence: %d\n" % checksum)
with open("python-world.txt", "r") as stream:
    assert stream.read() == "Python persistence: 8064\n"
assert os.stat("python-world.txt")[6] == len("Python persistence: 8064\n")
assert os.stat(".")[0] & 0xf000 == 0x4000
for i in range(1000):
    garbage = ["block" * 100, {"index": i}]
gc.collect()
print("Python / inheritance, imports, generators, GC, floats and files")
print("chunk=16x16, checksum=%d" % checksum)
print("bigint=%d" % (2 ** 80))
print("arg=" + (sys.argv[1] if len(sys.argv) > 1 else "none"))
print("PYTHON RUNTIME PASS")
