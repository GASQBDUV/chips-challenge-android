import sys, struct, collections
d = open(sys.argv[1], 'rb').read()
ntrk = struct.unpack('>H', d[10:12])[0]; p = 14
prog = collections.defaultdict(set); notes = collections.Counter(); drum = collections.Counter(); ctl = collections.Counter(); bend = 0; tempos = set()
chn = collections.Counter(); keys = collections.defaultdict(lambda: [127, 0])
for t in range(ntrk):
    ln = struct.unpack('>I', d[p+4:p+8])[0]; q = p + 8; e = q + ln; p = e; run = 0
    while q < e:
        while d[q] & 0x80: q += 1
        q += 1
        st = d[q]
        if st == 0xFF:
            ty = d[q+1]; l = d[q+2]
            if ty == 0x51: tempos.add(int.from_bytes(d[q+3:q+6], 'big'))
            q += 3 + l; continue
        if st in (0xF0, 0xF7): l = d[q+1]; q += 2 + l; continue
        if st & 0x80: run = st; q += 1
        ty = run & 0xF0; ch = run & 15
        if ty in (0xC0, 0xD0):
            if ty == 0xC0: prog[ch].add(d[q])
            q += 1
        else:
            a, b = d[q], d[q+1]; q += 2
            if ty == 0x90 and b:
                chn[ch] += 1
                if ch == 9: drum[a] += 1
                else: keys[ch][0] = min(keys[ch][0], a); keys[ch][1] = max(keys[ch][1], a)
            if ty == 0xB0: ctl[(ch, a)] += 1
            if ty == 0xE0: bend += 1
print(sys.argv[1], 'tempos', tempos, 'bend events', bend)
print(' notes per channel', dict(chn))
print(' programs', {c: sorted(v) for c, v in prog.items()})
print(' drum keys', dict(drum))
print(' key ranges', {c: tuple(v) for c, v in keys.items()})
print(' controllers', dict(ctl))
