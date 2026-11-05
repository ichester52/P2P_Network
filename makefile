# CS 4390 P2P project
#   make            build peer and tracker
#   make tracker    build only the tracker  -> TrackerFolder/tracker
#   make clean      remove built programs
#   make cleantorrents  delete tracker files (server must start with none for the final demo)

CC     = gcc
CFLAGS = -Wall -Wextra -pthread

TRACKER_SRC = tracker.c tracker_file.c md5.c
TRACKER_HDR = tracker.h md5.h

all: peer tracker

peer: PeerFolder/peer
tracker: TrackerFolder/tracker

PeerFolder/peer: peer.c
	mkdir -p PeerFolder
	$(CC) $(CFLAGS) peer.c -o PeerFolder/peer

TrackerFolder/tracker: $(TRACKER_SRC) $(TRACKER_HDR)
	mkdir -p TrackerFolder
	$(CC) $(CFLAGS) $(TRACKER_SRC) -o TrackerFolder/tracker

clean:
	rm -f PeerFolder/peer TrackerFolder/tracker

cleantorrents:
	rm -rf TrackerFolder/torrents

.PHONY: all peer tracker clean cleantorrents
