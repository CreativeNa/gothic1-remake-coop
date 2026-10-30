import struct, sys

path = sys.argv[1]
f = open(path, 'rb')
d = f.read()
sig, ver, nstreams, dir_rva = struct.unpack_from('<IIII', d, 0)
assert sig == 0x504d444d, 'not a minidump'
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', d, dir_rva + i * 12)
    streams[t] = (size, rva)
print('streams:', sorted(streams))

def utf16(rva):
    n = struct.unpack_from('<I', d, rva)[0]
    return d[rva + 4: rva + 4 + n].decode('utf-16le')

mods = []
if 4 in streams:
    size, rva = streams[4]
    n = struct.unpack_from('<I', d, rva)[0]
    for i in range(n):
        base, msize, csum, ts, name_rva = struct.unpack_from('<QIIII', d, rva + 4 + i * 108)
        mods.append((base, msize, utf16(name_rva)))

def which(addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return '%s+0x%x' % (name.split('\\')[-1], addr - base)
    return 'unknown'

if 6 in streams:
    size, rva = streams[6]
    tid, _ = struct.unpack_from('<II', d, rva)
    code, flags, rec, addr, nparams = struct.unpack_from('<IIQQI', d, rva + 8)
    params = struct.unpack_from('<15Q', d, rva + 8 + 4 + 4 + 8 + 8 + 4 + 4)
    print('exception thread %d code 0x%08x flags 0x%x address 0x%x -> %s' % (tid, code, flags, addr, which(addr)))
    print('params:', [hex(p) for p in params[:nparams]])
else:
    print('no exception stream')

if 3 in streams:                       # thread list: find the crashing thread's context and walk a few return addresses from its stack
    size, rva = streams[3]
    n = struct.unpack_from('<I', d, rva)[0]
    for i in range(n):
        off = rva + 4 + i * 48
        t, susp, prio_class, prio, teb, stack_start, stack_size, stack_rva, ctx_size, ctx_rva = struct.unpack_from('<IIIIQQIIII', d, off)
        if 6 in streams and t == tid:
            ctx = d[ctx_rva: ctx_rva + ctx_size]
            rip = struct.unpack_from('<Q', ctx, 0xF8)[0]
            rsp = struct.unpack_from('<Q', ctx, 0x98)[0]
            print('crashing thread RIP', hex(rip), which(rip), 'RSP', hex(rsp))
            stack = d[stack_rva: stack_rva + stack_size]
            print('stack scan (return-address-like values pointing into modules):')
            seen = 0
            for k in range(0, min(len(stack), 8192) - 8, 8):
                v = struct.unpack_from('<Q', stack, k)[0]
                w = which(v)
                if w != 'unknown' and not w.startswith('G1R-Win64-Shipping.exe') or (w.startswith('G1R-Win64-Shipping.exe') and seen < 60):
                    print('  +0x%x %s' % (k, w)); seen += 1
                if seen > 40: break

print('modules of interest:')
for base, size, name in mods:
    n = name.split('\\')[-1].lower()
    if any(s in n for s in ('coopdll', 'ue4ss', 'fyryny', 'couch', 'hook', 'xinput', 'dinput', 'rawinput', 'user32', 'dxgi', 'd3d')):
        print('  0x%x %s' % (base, name))
