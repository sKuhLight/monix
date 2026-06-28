#!/usr/bin/env python3
# dumpconst.py <hexaddr> <count> [int32|int8] — dump array at a vmaddr from the Mach-O
import sys, struct
B="iD.x86_64"
import os
if not os.path.exists(B): B="/tmp/claude-1000/-home-pascal-Dokumente-Repositorys-MixiD/1fe76c2e-8cdf-4cb9-a5a8-bf4f7b65749a/scratchpad/iD.x86_64"
data=open(B,'rb').read()
target=int(sys.argv[1],16); count=int(sys.argv[2]); typ=sys.argv[3] if len(sys.argv)>3 else 'int32'
ncmds=struct.unpack_from('<I',data,16)[0]; off=32; fileoff=None
for _ in range(ncmds):
    cmd,cmdsize=struct.unpack_from('<II',data,off)
    if cmd==0x19:
        nsects=struct.unpack_from('<I',data,off+64)[0]; so=off+72
        for _ in range(nsects):
            addr,size=struct.unpack_from('<QQ',data,so+32); foff=struct.unpack_from('<I',data,so+48)[0]
            if addr<=target<addr+size: fileoff=foff+(target-addr)
            so+=80
    off+=cmdsize
if fileoff is None: print("addr not in any section"); sys.exit(1)
if typ=='int8': print([data[fileoff+i] for i in range(count)])
else: print([struct.unpack_from('<i',data,fileoff+4*i)[0] for i in range(count)])
