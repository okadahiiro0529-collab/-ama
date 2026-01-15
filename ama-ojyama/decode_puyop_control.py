# decode_puyop_control.py
# usage: python decode_puyop_control.py "http://www.puyop.com/s/xxxxx_yyyyy"
import sys

CHAR = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ[]"
CHAR_INDEX = {c:i for i,c in enumerate(CHAR)}
CELL_NAMES = {0:"RED", 1:"GREEN", 2:"BLUE", 3:"YELLOW", 4:"GARBAGE(ctl)"}

def idx_of(ch):
    return CHAR_INDEX[ch]

def decode_control_full(ctrl):
    i = 0
    steps = []
    # iterate, handle special U sequences (mask + 'U') and normal 2-char pairs
    while i < len(ctrl):
        c = ctrl[i]
        if c == CHAR[56]:  # 'U' marker appearing alone (unexpected), treat as marker
            steps.append({"type":"MARKER_U_alone", "pos":i})
            i += 1
            continue
        # lookahead: if next char is 'U' => treat (c,'U') as garbage mask event
        if i+1 < len(ctrl) and ctrl[i+1] == CHAR[56]:
            mask_val = idx_of(c)
            # decode mask bits into columns (bit0 = leftmost in our assumption)
            bits = [(mask_val >> b) & 1 for b in range(6)]  # bit0..5
            steps.append({"type":"GARBAGE_MASK", "mask":mask_val, "bits":bits, "pos":i})
            i += 2
            continue
        # otherwise decode normal pair (2 chars -> one step)
        if i+1 < len(ctrl):
            a = idx_of(ctrl[i])
            b = idx_of(ctrl[i+1])
            # reconstruct 12-bit code as used originally: code = char0 + (char1<<6)
            code = a | (b << 6)
            pair_code = code & 0x7F
            first = pair_code // 5
            second = pair_code % 5
            placement_code = code >> 7
            x_plus1 = placement_code >> 2
            r = placement_code & 0x3
            x = x_plus1 - 1
            steps.append({"type":"PAIR", "pair":(first,second), "names":(CELL_NAMES.get(first,str(first)), CELL_NAMES.get(second,str(second))), "placement_x":x, "placement_r":r, "pos":i})
            i += 2
            continue
        # fallback: single char left
        steps.append({"type":"SINGLE_CHAR", "char":c, "pos":i})
        i += 1
    return steps

def print_steps(steps):
    for idx, s in enumerate(steps):
        if s["type"] == "PAIR":
            print(f"step {idx:03d}: PAIR {s['names']} -> placement x={s['placement_x']} r={s['placement_r']}")
        elif s["type"] == "GARBAGE_MASK":
            bits = s["bits"]
            # show bits as column list (left..right)
            cols = ",".join(str(i) for i,b in enumerate(bits) if b)
            print(f"step {idx:03d}: GARBAGE_MASK mask={s['mask']} bits(left..right)={bits} => columns with garbage: [{cols}]")
        else:
            print(f"step {idx:03d}: {s}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python decode_puyop_control.py URL")
        return
    url = sys.argv[1].strip()
    if "/s/" not in url or "_" not in url:
        print("Not a valid puyop URL:", url)
        return
    s = url.split("/s/")[1]
    field_code, ctrl = s.split("_", 1)
    print("Control length:", len(ctrl), "chars")
    steps = decode_control_full(ctrl)
    print_steps(steps)
    # quick stats
    cnt_pair = sum(1 for s in steps if s["type"]=="PAIR")
    cnt_mask = sum(1 for s in steps if s["type"]=="GARBAGE_MASK")
    print(f"\nTotal steps parsed: {len(steps)} (pairs={cnt_pair}, garbage_masks={cnt_mask})")

if __name__ == "__main__":
    main()