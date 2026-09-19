#!/usr/bin/env bash
# Print only the physics-relevant lines of decompiled functions (shapes, fixtures,
# joints, mass, filters, float constants) for quick digest.
# Usage: tools/decomp_digest.sh <file.c>...
for f in "$@"; do
  echo "=================== $(basename "$f")"
  grep -E 'SetAsBox|::Set\(|CreateFixture|CreateBody|CreateJoint|SetMassData|CollisionFilters::|JointDef::Initialize|param_[0-9] == |0x3[0-9a-f]{7}|0x4[0-9a-f]{7}|0xb[0-9a-f]{7}|0xc[0-9a-f]{7}|[0-9]\.[0-9]|case |return|HandleManager|Utils::|Apply|Set(Linear|Angular)|::Get' "$f" \
   | grep -v -E '^\s*(/\*|//)|0x3ba3d70a|0x3f800000;$|PTR__b2Shape' \
   | python3 -c '
import re,struct,sys
def conv(m):
    v=int(m.group(0),16)
    try:
        f=struct.unpack("<f",struct.pack("<I",v))[0]
    except Exception: return m.group(0)
    if abs(f)>1e-6 and abs(f)<1e6: return f"{m.group(0)}/*{f:.6g}*/"
    return m.group(0)
for line in sys.stdin:
    sys.stdout.write(re.sub(r"0x[0-9a-f]{8}",conv,line))
'
done
