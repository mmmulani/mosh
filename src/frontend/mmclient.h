#ifndef TERM_FRONTEND_MMCLIENT_H_
#define TERM_FRONTEND_MMCLIENT_H_

#include <sys/ioctl.h>
#include <termios.h>
#include <string>

#include <networktransport.h>

#include "commandstream.h"

class MMClient {
	private:
		std::string ip;
		int port;
		std::string key;

    // TODO: Use a different data structure for the remote side.
    // Maybe a TerminalResults?
		Network::Transport<Term::CommandStream, Term::CommandStream> *network;

	public:
		MMClient(const char *ip, int port, const char *key)
			: ip(ip), port(port), key(key) { }
		void init();
		void shutdown();
		void main();

		~MMClient() {
			if (network) {
				delete network;
			}
		}

};

#endif // TERM_FRONTEND_MMCLIENT_H_