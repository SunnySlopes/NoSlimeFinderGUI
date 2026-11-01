#pragma once

#include "types.hpp"
#include <string>

// 确保目录存在(不存在则创建)。成功或已存在返回 true。
bool ensureDir(const std::string& path);

// 从 JSON 配置文件读取参数(种子/半径/输出目录等)。
// 若文件不存在,自动写入一份带中文注释的默认配置,并返回 false 表示是新建的;
// 读取成功返回 true。无论哪种情况,p 都会被填成可用的参数。
bool loadConfig(const std::string& path, Params& p);

// 写出一份带中文注释的默认配置文件(默认种子为 0)。
void writeDefaultConfig(const std::string& path);
