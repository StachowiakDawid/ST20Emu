#!/usr/bin/env python3
"""Writes a Linux romfs image (fs/romfs, all fields big-endian, 16-byte aligned).

usage: mkromfs.py out.img ENTRY...
  dir:PATH                     directory
  file:PATH:HOSTFILE[:x]       regular file (x = executable)
  chr:PATH:MAJOR:MINOR         character device
  blk:PATH:MAJOR:MINOR         block device
  link:PATH:TARGET             hard link to the earlier entry TARGET (absolute path)
Parent directories are created as needed; every directory gets "." and "..".
"""
import struct, sys

HARD, DIR, REG, LNK, BLK, CHR = 0, 1, 2, 3, 4, 5
EXEC = 8


def pad16(b):
    return b + bytes(-len(b) % 16)


def checksum_fix(block):
    """value that makes the big-endian 32-bit word sum of block zero"""
    s = sum(struct.unpack(">%dI" % (len(block) // 4), block)) & 0xFFFFFFFF
    return (-s) & 0xFFFFFFFF


class Node:
    def __init__(self, name, kind, data=b"", spec=0, executable=False):
        self.name, self.kind, self.data, self.spec = name, kind, data, spec
        self.executable = executable
        self.children = []
        self.offset = 0


def build(entries):
    root = Node("", DIR)

    def find(path, create=True):
        node = root
        for part in [p for p in path.split("/") if p]:
            nxt = next((c for c in node.children if c.name == part), None)
            if nxt is None:
                if not create:
                    return None
                nxt = Node(part, DIR)
                node.children.append(nxt)
            node = nxt
        return node

    for e in entries:
        kind, rest = e.split(":", 1)
        if kind == "dir":
            find(rest)
            continue
        fields = rest.split(":")
        path = fields[0]
        parent, name = find(path.rsplit("/", 1)[0] or "/"), path.rsplit("/", 1)[1]
        if kind == "file":
            node = Node(name, REG, open(fields[1], "rb").read(),
                        executable=len(fields) > 2 and fields[2] == "x")
        elif kind == "link":
            target = find(fields[1], create=False)
            if target is None or target.kind == DIR:
                sys.exit("link target missing or a directory: " + e)
            node = Node(name, HARD)
            node.target = target
        elif kind in ("chr", "blk"):
            node = Node(name, CHR if kind == "chr" else BLK,
                        spec=int(fields[1]) << 16 | int(fields[2]))
        else:
            sys.exit("unknown entry " + e)
        parent.children.append(node)
    return root


def header_len(node):
    return 16 + len(pad16(node.name.encode() + b"\0"))


def layout(node, offset, parent):
    """assigns offsets; directory = header, then ".", "..", children (each recursive)"""
    node.offset = offset
    offset += header_len(node) + len(pad16(node.data))
    if node.kind == DIR:
        node.dot = Node(".", HARD)
        node.dotdot = Node("..", HARD)
        node.dot.offset = offset
        offset += header_len(node.dot)
        node.dotdot.offset = offset
        offset += header_len(node.dotdot)
        node.parent = parent or node
        for c in node.children:
            offset = layout(c, offset, node)
    return offset


def emit(node, out, next_off):
    def header(n, nxt, kind, spec, data):
        name = pad16(n.name.encode() + b"\0")
        h = bytearray(struct.pack(">IIII", nxt | kind, spec, len(data), 0) + name)
        struct.pack_into(">I", h, 12, checksum_fix(bytes(h)))
        out[n.offset:n.offset + len(h)] = h
        out[n.offset + len(h):n.offset + len(h) + len(data)] = data

    kind = node.kind | (EXEC if node.executable or node.kind == DIR else 0)
    if node.kind == DIR:
        header(node, next_off, kind, node.dot.offset, b"")
        kids = [node.dot, node.dotdot] + node.children
        for i, c in enumerate(kids):
            nxt = kids[i + 1].offset if i + 1 < len(kids) else 0
            if c is node.dot:
                header(c, nxt, HARD, node.offset, b"")
            elif c is node.dotdot:
                header(c, nxt, HARD, node.parent.offset, b"")
            else:
                emit(c, out, nxt)
    elif node.kind == HARD:
        header(node, next_off, HARD, node.target.offset, b"")
    else:
        header(node, next_off, kind, node.spec, node.data)


def main():
    root = build(sys.argv[2:])
    first = 32  # superblock: magic, size, checksum, volume name "rom st20" (16)
    size = layout(root, first, None)
    size = (size + 1023) & ~1023
    out = bytearray(size)
    emit(root, out, 0)
    sb = bytearray(b"-rom1fs-" + struct.pack(">II", size, 0) + pad16(b"rom st20\0"))
    out[0:len(sb)] = sb
    struct.pack_into(">I", out, 12, checksum_fix(bytes(out[:min(512, size)])))
    open(sys.argv[1], "wb").write(out)
    print(f"{sys.argv[1]}: {size} bytes")


if __name__ == "__main__":
    main()
