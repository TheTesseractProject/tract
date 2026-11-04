// Hex, binary literals and shift operators - the OS/register toolkit.
put(0xFF);          // 255
put(0b1010);        // 10
put(1 << 10);       // 1024
put(0xFF >> 4);     // 15
let mask = 0b1100 | 0b0011;
put(mask);          // 15
put(0xFF & 0x0F);   // 15
put(0x5A ^ 0x0F);   // 85
