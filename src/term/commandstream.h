#ifndef TERM_COMMANDSTREAM_H_
#define TERM_COMMANDSTREAM_H_

#include <deque>
#include <string>

namespace Term {
	class CommandStream {
    private:
      std::deque<std::string> actions;

    public:
      CommandStream() : actions() { }
      
      void push_back(std::string str) { actions.push_back(str); }  

      bool empty() const { return actions.empty(); }
      size_t size() const { return actions.size(); }
      const std::string *get_action(unsigned int i);

      /* interface for Network::Transport */
      void subtract(const CommandStream *prefix);
      std::string diff_from(const CommandStream &existing) const;
      void apply_string(std::string diff);
      bool operator==(const CommandStream &s) const {
        return actions == s.actions;
      }
      bool compare(const CommandStream &s) const { return false; }
	};
}

#endif // TERM_COMMANDSTREAM_H_