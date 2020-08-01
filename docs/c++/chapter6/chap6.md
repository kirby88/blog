# Chapters 6

## 6.1

Why `unsigned char c2 = 1256` will be truncated to 232? because an `unsigned char` is only 8 bits long, and can hold max 255 values. So if you try to set an `unsigned char` to 256, the value will be 1. 257 will be 2 and so on, until you reach 2*255+1 = 511, which again will overflow and be truncated to 1.