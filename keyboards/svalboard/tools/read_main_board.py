"""Read Sval MAIN_BOARD (0x38) and GET_INFO byte 20 from one Svalboard.
Usage: python read_main_board.py <serial> [repeat=1]"""
import hid, os, sys, time, json
SER = sys.argv[1]; REP = int(sys.argv[2]) if len(sys.argv) > 2 else 1
d = [d for d in hid.enumerate(0x303A, 0x4044)
     if d['serial_number'] == SER and d['usage_page'] == 0xFF61]
if not d: sys.exit(f"{SER}: not found")
h = hid.device(); h.open_path(d[0]['path'])
N = 32
def xfer(pkt, match):
    h.write(bytes([0]) + bytes(pkt) + bytes(N - len(pkt)))
    for _ in range(200):
        r = list(h.read(64, 200))
        if r and match(r): return r
    return None
nonce = list(os.urandom(20))
r = xfer([0xDD, 0, 0, 0, 0] + nonce, lambda r: r[0] == 0xDD and r[5:25] == nonce)
cid = r[25:29]
def sval(cmd):
    r = xfer([0xDD] + cid + [0xDF, cmd], lambda r: r[0] == 0xDD and r[1:5] == cid and r[5] == 0xDF)
    return r[5:]
BOARD = {0: "not identified", 1: "SvalMCUMini V1.0", 2: "V3.1/V3.2 (GP3 driven)", 3: "V3.0/V2B (GP3 floats)"}
CL = {0: "-", 1: "fast", 2: "ORIGINAL", 3: "dark"}
ROW = ["thumb", "index", "middle", "ring", "pinky"]
def half(b):
    if not b[0] & 1: return {"reported": False}
    f, mb, g, cl = b[0], b[1], b[2], b[3] | b[4] << 8
    return {"side": "unknown" if f & 0x20 else ("left" if f & 2 else "right"),
            "main_board": BOARD.get(mb, mb), "check_ran": bool(f & 4), "unsupported": bool(f & 8),
            "gp3_pd_pu_rel": f"{g & 1}{g >> 1 & 1}{g >> 2 & 1}", "gp3_agree": g >> 4,
            "clusters": {ROW[i]: CL[cl >> 2 * i & 3] for i in range(5)}}
info = sval(0x00)
print(f"{SER}  GET_INFO flags2=0x{info[19]:02x} flags3=0x{info[20]:02x}")
for i in range(REP):
    r = sval(0x38)
    if r[1] != 0x38: print("0x38 not supported:", r[:4]); break
    print(json.dumps({"format": r[2], "usb_half": half(r[3:8]), "other_half": half(r[8:13]),
                      "usb_boot_us": {"board": r[13] | r[14] << 8, "cluster_check": r[15] | r[16] << 8}}))
    if i + 1 < REP: time.sleep(0.6)
