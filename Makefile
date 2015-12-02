ipvs: ipvs.c
	gcc -fPIC -shared -o ipvs.so ipvs.c -I../../../include
