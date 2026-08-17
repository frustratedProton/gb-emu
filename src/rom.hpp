#pragma once

#include "types.hpp"
#include <string>
#include <vector>

std::vector<u8> load_rom(const std::string &path);
bool valid_header_checksum(const std::vector<u8> &rom);
void print_title(const std::vector<u8> &rom);