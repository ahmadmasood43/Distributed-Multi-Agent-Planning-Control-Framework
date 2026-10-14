# Distributed Multi-Agent Planning & Control Framework

A modular C++17 framework for coordinating autonomous agents in dynamic environments. It provides reusable components for:

- Task allocation
- Graph-based path planning
- Behavior-tree control
- Resource coordination and conflict resolution
- Failure recovery

Planning runs concurrently on a thread pool. The same coordinator and agent logic runs in a single process or as separate ROS 2 processes.

Example scenarios: warehouse robots, a delivery fleet, and multi-drone search.

> Status: design phase.

- [Design](docs/DESIGN.md): architecture, interfaces, threading model, protocols
- [Roadmap](docs/ROADMAP.md): milestones with acceptance criteria
