#pragma once
#include <iostream>
#include <string>
inline bool version_requested(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--version") return false;
    std::cout << "{\"version\":\"" << AOF_VERSION_STRING
        << "\",\"strategy_schema\":4,\"checkpoint_schema\":4,\"max_players\":4,\"focused_audit\":true,\"fixed_rake\":true}\n";
    return true;
}
