with open('/home/andrew/Projects/8086tiny/bios', 'rb') as f:
    data = f.read()
    for i in range(0, min(len(data), 400), 16):
        line = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in line)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
        print(f'{i:04x}: {hex_str:<48} {ascii_str}')

# Check specific offsets
print("\n=== Offset 0x100 (256) ===")
for i in range(0x100, min(0x100 + 16, len(data)), 16):
    line = data[i:i+16]
    hex_str = ' '.join(f'{b:02x}' for b in line)
    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
    print(f'{i:04x}: {hex_str:<48} {ascii_str}')

print("\n=== Offset 0x152 (338) ===")
for i in range(0x152, min(0x152 + 16, len(data)), 16):
    line = data[i:i+16]
    hex_str = ' '.join(f'{b:02x}' for b in line)
    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
    print(f'{i:04x}: {hex_str:<48} {ascii_str}')

# Check first 256 bytes for zeros
print("\n=== First 256 bytes (org 100h padding) ===")
zeros = all(b == 0 for b in data[:256])
print(f"All zeros: {zeros}")
if not zeros:
    for i in range(0, 256, 16):
        line = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in line)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
        print(f'{i:04x}: {hex_str:<48} {ascii_str}')