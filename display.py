import cv2
import numpy as np

h, w, c = 2160, 3840, 4
fb = np.memmap('/dev/fb0', dtype='uint8',mode='w+', shape=(h,w,c)) 

filepath = "output.yuv"

f = open(filepath, 'rb')
raw = f.read(int(w*h*1.5))

yuv = np.frombuffer(raw, dtype=np.uint8)
shape = (int(h*1.5), w)
yuv = yuv.reshape(shape)

bgr = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGRA_NV12)

fb[:] = bgr[:]
