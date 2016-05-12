#!/usr/bin/python

import struct


file_payload = open('log_coordinates.txt', 'r+')

n = 2

for line in file_payload:			
	last = line

payload = [last[i:i+n] for i in range(0, len(last), n)]
payload.pop()

#payload = ["42", "42", "53", "71", "40", "F7", "CF", "1A"]
latitude_list = payload[:len(payload)/2]
longitude_list = payload[len(payload)/2:]
latitude = ""
longitude = ""
for value in latitude_list:
	latitude += str(value)

for value in longitude_list:
	longitude += str(value)

latitude_real = struct.unpack('!f', latitude.decode('hex'))[0]
longitude_real = struct.unpack('!f', longitude.decode('hex'))[0]

print("latitude : " + str(latitude_real) + " | longitude : " + str(longitude_real))

file_payload.write(str(latitude_real) + "-" + str(longitude_real) + "\n")

file_payload.close()
