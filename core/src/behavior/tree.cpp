#include "mapc/behavior/tree.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace mapc::bt {

namespace {

class TreeTickGuard {
public:
    explicit TreeTickGuard(bool& flag) noexcept : flag_(&flag) { *flag_ = true; }
    TreeTickGuard(const TreeTickGuard&) = delete;
    TreeTickGuard& operator=(const TreeTickGuard&) = delete;
    TreeTickGuard(TreeTickGuard&&) = delete;
    TreeTickGuard& operator=(TreeTickGuard&&) = delete;
    ~TreeTickGuard() { *flag_ = false; }

private:
    bool* flag_;
};

void dumpNode(std::ostringstream& out, const Node& node, const std::string& prefix, bool is_root,
              bool is_last) {
    std::string child_prefix = prefix;
    if (!is_root) {
        out << prefix << (is_last ? "`-- " : "+-- ");
        child_prefix += is_last ? "    " : "|   ";
    }
    out << node.typeName();
    if (!node.name().empty()) {
        out << " \"" << node.name() << '"';
    }
    out << " [" << toString(node.state()) << "]\n";
    const std::size_t count = node.childCount();
    for (std::size_t i = 0; i < count; ++i) {
        dumpNode(out, node.child(i), child_prefix, false, i + 1 == count);
    }
}

}  // namespace

Tree::Tree(NodePtr root) : root_(std::move(root)) {
    if (!root_) {
        throw std::invalid_argument("bt::Tree: root is null");
    }
}

Tree::~Tree() {
    root_->halt();
}

Status Tree::tick(Tick now) {
    if (ticking_) {
        throw std::logic_error("bt::Tree::tick called from inside a tick of the same tree");
    }
    if (last_tick_ && now < *last_tick_) {
        throw std::invalid_argument("bt::Tree::tick: time went backwards");
    }
    last_tick_ = now;
    const TreeTickGuard guard(ticking_);
    TickContext context(now, blackboard_);
    return root_->tick(context);
}

void Tree::halt() {
    if (ticking_) {
        throw std::logic_error("bt::Tree::halt called from inside a tick of the same tree");
    }
    root_->halt();
}

std::string Tree::dump() const {
    return dumpTree(*root_);
}

std::string dumpTree(const Node& root) {
    std::ostringstream out;
    dumpNode(out, root, "", true, true);
    return out.str();
}

}  // namespace mapc::bt
