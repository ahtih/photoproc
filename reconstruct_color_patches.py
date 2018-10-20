#!/usr/bin/python

# This program can be used to check the accuracy of color patches
#  detection (photoproc -matrix <imagefile>). It reconstructs an image
#  based on color patches detection output.

import sys,re,Image,ImageDraw

N=14
output_patch_size=(160,90)

color_patches=[]

for line in open(sys.argv[1],'r'):
	m=re.match(r'^ *([0-9]+),([0-9]+),([0-9]+) *$',line)
	if not m:
		continue

	color_patches.append(map(int,m.groups()))

if len(color_patches) != N*N:
	print 'Wrong number of input lines (%d)' % (len(color_patches),)
	exit(1)

im=Image.new('RGB',(output_patch_size[0]*N,output_patch_size[1]*N))
draw=ImageDraw.Draw(im)

for idx,rgb in enumerate(color_patches):
	x_idx=idx % N
	y_idx=idx / N
	draw.rectangle((x_idx * output_patch_size[0],
					y_idx * output_patch_size[1],
					(x_idx+1) * output_patch_size[0] - 1,
					(y_idx+1) * output_patch_size[1] - 1),
					fill=(rgb[0] >> 8,rgb[1] >> 8,rgb[2] >> 8))

del draw
im.save(sys.argv[2],'PNG')
