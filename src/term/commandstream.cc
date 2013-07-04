#include "commandstream.h"

#include "commandmessage.pb.h"

namespace Term {

void CommandStream::subtract(const CommandStream *prefix) {
  for (std::deque<std::string>::const_iterator i = prefix->actions.begin();
    i != prefix->actions.end();
    i++) {
    actions.pop_front();
  }
}


std::string CommandStream::diff_from(const CommandStream &existing) const {
  std::deque<std::string>::const_iterator my_it = actions.begin();

  for (std::deque<std::string>::const_iterator i = existing.actions.begin();
    i != existing.actions.end();
    i++) {
    my_it++;
  }

  TermBuffers::CommandMessage output;

  while (my_it != actions.end()) {
    TermBuffers::Instruction *new_inst = output.add_instruction();
    new_inst->MutableExtension(TermBuffers::general)->set_payload(*my_it);

    my_it++;
  }

  return output.SerializeAsString();
}

void CommandStream::apply_string(std::string diff) {
  TermBuffers::CommandMessage input;
  input.ParseFromString(diff);

  for (int i = 0; i < input.instruction_size(); i++) {
    std::string message =
      input.instruction(i).GetExtension(TermBuffers::general).payload();
    actions.push_back(message);
  }
}

const std::string *CommandStream::get_action(unsigned int i) {
  return &actions[i];
}

} // namespace Term