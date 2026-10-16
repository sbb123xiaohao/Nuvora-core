"""Shared Python module; deterministic integer grid, not a Minecraft client."""
def chunk(seed):
    blocks = []
    for z in range(16):
        row = []
        for x in range(16):
            seed = (seed * 1664525 + 1013904223) & 0xffffffff
            row.append(seed % 64)
        blocks.append(row)
    return blocks
