#pragma once

#include "proc_metrics.hpp"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ProcessTreeIndex {
    std::vector<int> roots;
    std::unordered_map<int, std::vector<int>> children_by_ppid;
};

inline ProcessTreeIndex build_process_tree_index(const std::vector<ProcessInfo>& processes) {
    ProcessTreeIndex index;
    std::set<int> pids;
    for (const auto& p : processes) pids.insert(p.pid);
    for (const auto& p : processes) {
        if (p.ppid != p.pid && pids.contains(p.ppid))
            index.children_by_ppid[p.ppid].push_back(p.pid);
        else
            index.roots.push_back(p.pid);
    }
    return index;
}

class FlatProcessTreeSource {
public:
    void update(std::vector<ProcessInfo> processes) {
        processes_ = std::move(processes);
        index_ = build_process_tree_index(processes_);
    }

    size_t root_count() const { return index_.roots.size(); }
    uint64_t root_at(size_t i) const { return static_cast<uint64_t>(index_.roots[i]); }

    size_t child_count(uint64_t id) const {
        auto it = index_.children_by_ppid.find(static_cast<int>(id));
        return it != index_.children_by_ppid.end() ? it->second.size() : 0;
    }

    uint64_t child_at(uint64_t id, size_t i) const {
        return static_cast<uint64_t>(index_.children_by_ppid.at(static_cast<int>(id))[i]);
    }

    std::string label(uint64_t id) const {
        const ProcessInfo* p = find(static_cast<int>(id));
        if (!p) return "?";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s (%d) %.1f%%", p->name.c_str(), p->pid, p->cpu_percent);
        return buf;
    }

    bool has_children(uint64_t id) const { return child_count(id) > 0; }

private:
    const ProcessInfo* find(int pid) const {
        for (const auto& p : processes_) if (p.pid == pid) return &p;
        return nullptr;
    }

    std::vector<ProcessInfo> processes_;
    ProcessTreeIndex index_;
};
