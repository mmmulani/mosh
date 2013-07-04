#include "config.h"

#include "mmclient.h"

#include "select.h"
#include "networktransport.cc"

void MMClient::init() {
  
}

void MMClient::shutdown() {
  
}

void MMClient::main() {
  Term::CommandStream blank_stream;
  Term::CommandStream blank_results;

  network =
    new Network::Transport<Term::CommandStream, Term::CommandStream>(
      blank_stream, blank_results, key.c_str(), ip.c_str(), port
    );
  network->set_send_delay(1);

  Select &sel = Select::get_instance();
  while (1) {
    try {
      //output_new_frame

      sel.clear_fds();
      sel.add_fd(STDIN_FILENO);
      std::vector<int> fd_list = network->fds();
      for (std::vector<int>::const_iterator it = fd_list.begin();
        it != fd_list.end();
        it++) {
        sel.add_fd(*it);
      }

      int active_fds = sel.select(250);
      if (active_fds < 0) {
        fprintf(stderr, "active fds error\n");
        break;
      }

      bool read_from_network = false;
      for (std::vector<int>::const_iterator it = fd_list.begin();
        it != fd_list.end();
        it++) {
        if (sel.read(*it)) {
          read_from_network = true;
        }
      }

      if (read_from_network) {
        network->recv();
      }

      if (sel.read(STDIN_FILENO)) {
        // For now just send a string of "DEADBEEF" as the message but we should
        // actually be reading stdin and sending messages based on that.
        network->get_current_state().push_back(std::string("DEADBEEF"));
      }

      network->tick();
    } catch (...) {

    }
  }
}