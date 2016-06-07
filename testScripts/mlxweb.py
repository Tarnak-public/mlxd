#!/usr/bin/env python

# Demo code
#
#   simple demonstration script  showing real-time thermal Imaging
#   using the MLX90621 16x4 thermopile array and the mlxd daemon
#
#   Copyright (C) 2015 Chuck Werbick
#
#
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software Foundation,
#   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
import picamera
import numpy as np
import subprocess
import skimage
from skimage import io, exposure, transform, img_as_float, img_as_ubyte
import matplotlib.pyplot as plt
from time import sleep
from pprint import pprint

# IR registration parameters
ROT = np.deg2rad(90)
SCALE = (36.2, 36.4)
OFFSET = (530, 170)


def getImage():
    fn = r'/home/pi/tmp.jpg'
    proc = subprocess.Popen('raspistill -o %s -w 640 -h 480 -n -t 3' % (fn),
                            shell=True, stderr=subprocess.STDOUT)
    proc.wait()
    im = io.imread(fn, as_grey=True)
    im = exposure.equalize_hist(im)
    return skimage.img_as_ubyte(im)


def get_overlay(fifo):
    # get the whole FIFO
    ir_raw = fifo.read()
    # trim to 128 bytes
    ir_trimmed = ir_raw[0:128]
    # go all numpy on it
    ir = np.frombuffer(ir_trimmed, np.uint16)
    pprint(ir)
    # set the array shape to the sensor shape (16x4)
    print('test1')
    ir = ir.reshape((16, 4))[::-1, ::-1]
    print('test2')
    ir = img_as_float(ir)
    print('test3')
    # stretch contrast on our heat map
    print('test4')
    p2, p98 = np.percentile(ir, (2, 98))
    print('test5')
    pprint(p2)
    pprint(p98)
    ir = exposure.rescale_intensity(ir, in_range=(p2, p98))
    print('test6')
    # increase even further? (optional)
    # ir = exposure.equalize_hist(ir)

    # turn our array into pretty colors
    print('test7')
    cmap = plt.get_cmap('spectral')
    print('test8')
    rgba_img = cmap(ir)
    print('test9')
    rgb_img = np.delete(rgba_img, 3, 2)
    print('test10')

    # align the IR array with the camera
    print('test11')
    tform = transform.AffineTransform(
        scale=SCALE, rotation=ROT, translation=OFFSET)
    print('test12')
    ir_aligned = transform.warp(
        rgb_img, tform.inverse, mode='constant', output_shape=im.shape)
    print('test13')
    # turn it back into a ubyte so it'll display on the preview overlay
    print('test14')
    ir_byte = img_as_ubyte(ir_aligned)
    print('test15')
    # return buffer
    return np.getbuffer(ir_byte)


im = getImage()

with picamera.PiCamera() as camera:
    camera.led = False
    camera.resolution = (640, 480)
    camera.framerate = 20
    camera.start_preview()

    # get the temperature array, and align with the image
    fifo = open('/var/run/mlx90621.sock', 'r')
    o = camera.add_overlay(get_overlay(fifo), layer=3, alpha=90)

    # update loop
    while True:
        sleep(0.25)
        o.update(get_overlay(fifo))

    print('Error! Closing...')
    camera.remove_overlay(o)
    fifo.close()
