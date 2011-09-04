all: jarchcd2 jarchdvd2 jarchdvdkeys config.h jarch-videocd-to-mpg jarch-verify-data jarchdvd-out-iso

CFLAGS = -DLINUX

clean:
	rm -f *.exe *.o jarchcd2 jarchdvd2 jarchdvdkeys
	rm -f -R Debug Release "Debug WNASPI32" "Release WNASPI32" jarch-videocd-to-mpg jarch-verify-data
	rm -f *.ncb *.opt *.plg *.ilk
	rm -f *~

install: jarchdvd2
	mkdir -p $(ROOT)/usr/bin/
	cp -fv jarchcd2 $(ROOT)/usr/bin/
	cp -fv jarchdvd2 $(ROOT)/usr/bin/
	cp -fv jarchdvdkeys $(ROOT)/usr/bin/
	mkdir -p $(ROOT)/usr/man/man1
	cp -fv jarchdvd2.1 $(ROOT)/usr/man/man1/
	cp -fv jarchdvd-out-iso $(ROOT)/usr/bin/
	cp -fv jarchdvdkeys.1 $(ROOT)/usr/man/man1/
	cp -fv jarch-verify-data jarch-videocd-to-mpg jarch-{audio-to-wav,clear,rip,do-rip,rip-ns,do-rip-ns,rip-ns-persistent,do-rip-ns-persistent} $(ROOT)/usr/bin/

JARCHCD_OBJLIST = bitchin.o blockio.o blockio_iokit.o blockio_ntscsi.o blockio_std.o blockio_packet.o blockio_sg.o jarchcd.o util.o rippedmap.o lsimage.o
JARCHDVD_OBJLIST = bitchin.o blockio.o blockio_iokit.o blockio_ntscsi.o blockio_std.o blockio_packet.o blockio_sg.o jarchdvd.o util.o keystore.o rippedmap.o lsimage.o mediagather.o css-auth.o css-cipher.o dvd-auth.o ripdvd.o ripdvd_dvdvideo.o udf.o
JARCHDVD_OUT_ISO_OBJLIST = bitchin.o jarchdvd-out-iso.o util.o keystore.o rippedmap.o ripdvd.o ripdvd_dvdvideo.o lsimage.o css-auth.o css-cipher.o dvd-auth.o udf.o

jarchdvd-out-iso: $(JARCHDVD_OUT_ISO_OBJLIST)
	g++ -o jarchdvd-out-iso $(JARCHDVD_OUT_ISO_OBJLIST)

jarchcd2: $(JARCHCD_OBJLIST)
	g++ -o jarchcd2 $(JARCHCD_OBJLIST)

jarchdvd2: $(JARCHDVD_OBJLIST)
	g++ -o jarchdvd2 $(JARCHDVD_OBJLIST)

jarchdvdkeys: jarchdvdkeys.o keystore.o bitchin.o util.o
	g++ -o jarchdvdkeys jarchdvdkeys.o keystore.o bitchin.o util.o

jarch-verify-data: jarch-verify-data.c
	gcc $(CFLAGS) -o $@ $<

jarchdvdkeys.o: jarchdvdkeys.cpp
	g++ $(CFLAGS) -c -o jarchdvdkeys.o jarchdvdkeys.cpp

util.o: util.cpp
	g++ $(CFLAGS) -c -o util.o util.cpp

jarchcd.o: jarchcd.cpp
	g++ $(CFLAGS) -c -o jarchcd.o jarchcd.cpp

jarchdvd.o: jarchdvd.cpp
	g++ $(CFLAGS) -c -o jarchdvd.o jarchdvd.cpp

jarchdvd-out-iso.o: jarchdvd-out-iso.cpp
	g++ $(CFLAGS) -c -o jarchdvd-out-iso.o jarchdvd-out-iso.cpp

bitchin.o: bitchin.cpp
	g++ $(CFLAGS) -c -o bitchin.o bitchin.cpp

blockio.o: blockio.cpp
	g++ $(CFLAGS) -c -o blockio.o blockio.cpp

blockio_sg.o: blockio_sg.cpp
	g++ $(CFLAGS) -c -o blockio_sg.o blockio_sg.cpp

blockio_std.o: blockio_std.cpp
	g++ $(CFLAGS) -c -o blockio_std.o blockio_std.cpp

blockio_packet.o: blockio_packet.cpp
	g++ $(CFLAGS) -c -o blockio_packet.o blockio_packet.cpp

blockio_ntscsi.o: blockio_ntscsi.cpp
	g++ $(CFLAGS) -c -o blockio_ntscsi.o blockio_ntscsi.cpp

blockio_iokit.o: blockio_iokit.cpp
	g++ $(CFLAGS) -c -o blockio_iokit.o blockio_iokit.cpp

lsimage.o: lsimage.cpp
	g++ $(CFLAGS) -c -o lsimage.o lsimage.cpp

keystore.o: keystore.cpp
	g++ $(CFLAGS) -c -o keystore.o keystore.cpp

rippedmap.o: rippedmap.cpp
	g++ $(CFLAGS) -c -o rippedmap.o rippedmap.cpp

mediagather.o: mediagather.cpp
	g++ $(CFLAGS) -c -o mediagather.o mediagather.cpp

css-auth.o: css-auth.cpp
	g++ $(CFLAGS) -c -o css-auth.o css-auth.cpp

css-cipher.o: css-cipher.cpp
	g++ $(CFLAGS) -c -o css-cipher.o css-cipher.cpp

dvd-auth.o: dvd-auth.cpp
	g++ $(CFLAGS) -c -o dvd-auth.o dvd-auth.cpp

ripdvd.o: ripdvd.cpp
	g++ $(CFLAGS) -c -o ripdvd.o ripdvd.cpp

jarch-videocd-to-mpg.o: jarch-videocd-to-mpg.c
	gcc $(CFLAGS) -c -o jarch-videocd-to-mpg.o jarch-videocd-to-mpg.c

jarch-videocd-to-mpg: jarch-videocd-to-mpg.o
	gcc $(CFLAGS) -o jarch-videocd-to-mpg jarch-videocd-to-mpg.o

ripdvd_dvdvideo.o: ripdvd_dvdvideo.cpp
	g++ $(CFLAGS) -c -o ripdvd_dvdvideo.o ripdvd_dvdvideo.cpp

udf.o: udf.cpp
	g++ $(CFLAGS) -c -o udf.o udf.cpp

again:
	make clean && make

