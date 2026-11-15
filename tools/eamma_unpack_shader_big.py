# EN-1 (FINDINGS-en1-shaders.md): unpack data\xenon-gen\bigs\shader_big.ast (BGFA, 157 zlib RNAFX effects).
# Usage: python -I tools/eamma_unpack_shader_big.py <shader_big.ast> <out dir>. Writes <name>.rnafx + _index.json.
import zlib,sys,os,json
src,out=sys.argv[1],sys.argv[2]
d=open(src,'rb').read()
n=int.from_bytes(d[12:16],'little')
rows=[]
for i in range(n):
  e=d[0x44+i*77:0x44+(i+1)*77]
  off=int.from_bytes(e[9:12],'little')*8
  name=e[17:].split(b'\0')[0].decode()
  o=zlib.decompressobj(); u=o.decompress(d[off:off+0x40000]); csz=0x40000-len(o.unused_data) if o.eof else None
  rows.append(dict(i=i,name=name,hash=e[1:9][::-1].hex(),off=off,csize=csz,usize=len(u),tail=e[12:17].hex()))
  open(os.path.join(out,name+'.rnafx'),'wb').write(u)
json.dump(rows,open(os.path.join(out,'_index.json'),'w'),indent=1)
print(n, sum(r['usize'] for r in rows))
for r in rows[:8]: print(r)
