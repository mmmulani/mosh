AM_CPPFLAGS = -I$(srcdir)/../util -I../protobufs $(protobuf_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS)

noinst_LIBRARIES = libmoshterm.a

libmoshterm_a_SOURCES = commandstream.cc commandstream.h
