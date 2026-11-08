"""Generate an asset-free Build v7 map for native map/controller integration checks."""
import argparse
from pathlib import Path
import struct


def make_map(path):
    sectors, walls, sprites = [], [], []

    def room(x0, x1, floor=128, ceiling=0, tag=0):
        index = len(sectors)
        sectors.append((len(walls), floor, ceiling, tag))
        points = [(x0, 0), (x1, 0), (x1, 4096), (x0, 4096)]
        for n, (x, y) in enumerate(points):
            walls.append([x, y, index * 4 + (n + 1) % 4, -1, -1, 461, 0])
        return index

    def link(a, b):
        walls[a * 4 + 1][3:5] = [b * 4 + 3, b]
        walls[b * 4 + 3][3:5] = [a * 4 + 1, a]

    def sprite(sector, tile, lotag, hitag):
        start, floor, _, _ = sectors[sector]
        x = (walls[start][0] + walls[start + 1][0]) // 2
        sprites.append(struct.pack('<iiiHhbBBBBBbbhhhhhhhhhh', x, 2048, floor * 256,
                                   0, tile, 0, 0, 32, 0, 32, 32, 0, 0,
                                   sector, 0, 0, -1, 0, 0, 0, lotag, hitag, 0))

    room(0, 4096)
    room(4096, 6144, ceiling=128, tag=20)  # use-operated ceiling door
    room(6144, 10240)
    room(10240, 12288, tag=16)  # floor lift to raised neighbor
    room(12288, 16384, floor=96)
    for a in range(4):
        link(a, a + 1)
    for x in (32768, 40960):
        room(x, x + 4096)
    sprite(5, 1, 7, 700)
    sprite(6, 1, 7, 700)
    room(49152, 53248, tag=1)
    room(57344, 61440, floor=256, ceiling=128, tag=2)
    sprite(7, 1, 7, 800)
    sprite(8, 1, 7, 800)
    room(65536, 69632)
    room(69632, 71680, ceiling=128, tag=20)
    room(71680, 75776, tag=-2)  # native delayed-exit sector
    link(9, 10)
    link(10, 11)
    walls[9 * 4][5:] = [130, 900]  # blue access switch, north wall
    sprite(10, 2, 900, 0)  # native activator controls the second door

    data = bytearray(struct.pack('<iiiihhH', 7, 3584, 2048, 88 * 256, 0, 0, len(sectors)))
    for n, (start, floor, ceiling, tag) in enumerate(sectors):
        record = struct.pack('<hhiiHHHhbBBBHhbBBBBBhhh', start, 4, ceiling * 256, floor * 256,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             tag, 32123 if n == 0 else 0, 1024)
        assert len(record) == 40
        data.extend(record)
    data.extend(struct.pack('<H', len(walls)))
    for x, y, point, nextwall, nextsector, tile, tag in walls:
        record = struct.pack('<iihhhHhhbBBBBBhhh', x, y, point, nextwall, nextsector, 0,
                             tile, 0, 0, 0, 8, 8, 0, 0, tag, 0, -1)
        assert len(record) == 32
        data.extend(record)
    data.extend(struct.pack('<H', len(sprites)))
    assert all(len(sprite) == 44 for sprite in sprites)
    data.extend(b''.join(sprites))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    make_map(parser.parse_args().output)
