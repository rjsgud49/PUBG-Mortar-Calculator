#pragma once

#include "vision.hpp"

#include <filesystem>

std::filesystem::path find_project_root(const std::filesystem::path& start);
void load_settings(const std::filesystem::path& file, Settings& settings);
void save_settings(const std::filesystem::path& file, const Settings& settings);
