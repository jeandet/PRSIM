#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/process_tree_source.hpp"

TEST_CASE("build_process_tree_index groups children under their parent pid") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init"});
    procs.push_back(ProcessInfo{.pid = 2, .ppid = 1, .name = "shell"});
    procs.push_back(ProcessInfo{.pid = 3, .ppid = 1, .name = "editor"});
    procs.push_back(ProcessInfo{.pid = 4, .ppid = 2, .name = "grep"});

    ProcessTreeIndex idx = build_process_tree_index(procs);

    REQUIRE(idx.roots.size() == 1);
    CHECK(idx.roots[0] == 1);
    REQUIRE(idx.children_by_ppid.at(1).size() == 2);
    CHECK(idx.children_by_ppid.at(2) == std::vector<int>{4});
}

TEST_CASE("build_process_tree_index reparents a process whose ppid is not in the list to root") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init"});
    procs.push_back(ProcessInfo{.pid = 5, .ppid = 999, .name = "orphan"}); // 999 not present

    ProcessTreeIndex idx = build_process_tree_index(procs);

    CHECK(idx.roots.size() == 2);
    CHECK(std::find(idx.roots.begin(), idx.roots.end(), 5) != idx.roots.end());
}

TEST_CASE("build_process_tree_index treats a self-parented pid as a root, not a self-loop") {
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 1, .name = "weird-init"});

    ProcessTreeIndex idx = build_process_tree_index(procs);

    CHECK(idx.roots.size() == 1);
    CHECK(idx.roots[0] == 1);
    CHECK(idx.children_by_ppid.empty());
}

TEST_CASE("FlatProcessTreeSource exposes the index through the TreeStorage-shaped methods") {
    FlatProcessTreeSource source;
    std::vector<ProcessInfo> procs;
    procs.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init", .cpu_percent = 0.5f});
    procs.push_back(ProcessInfo{.pid = 2, .ppid = 1, .name = "shell", .cpu_percent = 1.5f});
    source.update(procs);

    REQUIRE(source.root_count() == 1);
    uint64_t root = source.root_at(0);
    CHECK(root == 1);
    REQUIRE(source.child_count(root) == 1);
    CHECK(source.child_at(root, 0) == 2);
    CHECK(source.has_children(root));
    CHECK_FALSE(source.has_children(2));
    CHECK(source.label(root).find("init") != std::string::npos);
}
