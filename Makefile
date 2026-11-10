all: peer tracker

peer: peer.c
	gcc peer.c -o ./PeerFolder/peer

tracker: tracker.c
	gcc tracker.c -o ./TrackerFolder/tracker -lcrypto

clean:
	rm -f peer tracker