import ctypes
libc = ctypes.CDLL("libc.so.6")

x = 0
while x < 1000000:
    libc.strlen(b"test")
    x += 1
