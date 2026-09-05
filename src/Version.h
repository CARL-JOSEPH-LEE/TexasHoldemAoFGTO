#pragma once
#include <iostream>
#include <string>
inline bool version_requested(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--version") return false;
    std::cout << "{\"version\":\"" << AOF_VERSION_STRING
        << "\",\"strategy_schema\":3,\"checkpoint_schema\":3,\"max_players\":4,\"focused_audit\":true}\n";
    return true;
}
