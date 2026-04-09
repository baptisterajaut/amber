#ifndef SEQUENCE_FWD_H
#define SEQUENCE_FWD_H

#include <memory>

class Sequence;
using SequencePtr = std::shared_ptr<Sequence>;

namespace amber {
extern SequencePtr ActiveSequence;
}

#endif  // SEQUENCE_FWD_H
