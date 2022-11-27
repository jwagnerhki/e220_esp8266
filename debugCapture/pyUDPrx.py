#!/usr/bin/python

import socket

ESP8226_UDP_DEST_PORT = 3220

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind( ('', ESP8226_UDP_DEST_PORT) )

fout = open("sml.bin", "w")

n = 0
#while True:
while n < 1:
	data, addr = sock.recvfrom(1024)
	hexstr = data.encode('hex')
	print("From %s received %d bytes : %s" % (str(addr), len(data), hexstr))
	fout.write(data)
	n += 1

fout.close()

