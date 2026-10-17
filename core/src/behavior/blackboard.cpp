#include "mapc/behavior/blackboard.hpp"

#include <stdexcept>
#include <string>

namespace mapc::bt {

bool Blackboard::erase(std::string_view name) {
    const auto it = entries_.find(name);
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    return true;
}

std::vector<std::string> Blackboard::names() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& entry : entries_) {
        names.push_back(entry.first);
    }
    return names;
}

void Blackboard::throwMissing(std::string_view name) {
    throw std::out_of_range("Blackboard: no entry '" + std::string(name) + "'");
}

void Blackboard::throwTypeMismatch(std::string_view name, const std::type_info& requested,
                                   const std::type_info& stored) {
    throw BlackboardTypeError("Blackboard: entry '" + std::string(name) + "' holds " +
                              stored.name() + ", not " + requested.name());
}

}  // namespace mapc::bt
