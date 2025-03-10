// Copyright (c) 2025 Advanced Micro Devices, Inc
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef __UTILS_H_
#define __UTILS_H_

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>

namespace Utils {

std::string get_env_var(const std::string &var,
                        const std::string &default_val = {});
int64_t ceil_for_me(int64_t x, int64_t y);
size_t get_size_of_type(const std::string &type);

// Align the 'n' to the multiple of 'A'
// new_n >= n
// new_n % A = 0
size_t align_to_next(size_t n, size_t alignment);

template <typename T>
static void write_buffer_to_file(T *buf, size_t buf_size, std::string fname) {
  std::ofstream ofs;
  ofs.open("./logs/" + fname);
  for (size_t i = 0; i < buf_size; i++) {
    ofs << std::to_string(buf[i]) << "\n";
  }
  ofs.close();
}

static std::string NormWinPath(const std::string &path) {
  if (path.find("\\\\") != std::string::npos) {
    return path; // Already normalized
  }
  std::string normalizedPath = path;

  // Replace all forward slashes '/' with backward slashes '\'
  std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

  // Add an extra backslash after each existing backslash
  size_t pos = 0;
  while ((pos = normalizedPath.find("\\", pos)) != std::string::npos) {
    normalizedPath.insert(pos, "\\");
    pos += 2; // Move past the double backslashes
  }

  return normalizedPath;
}

/// @brief Concat multiple vectors to a single vector
template <typename Vec, typename... Vecs>
static Vec concat_vectors(const Vec &vec0, const Vecs &...vecs) {
  auto sizeof_vec = [](const Vec &vec) -> size_t { return vec.size(); };
  auto concat_vec = [](Vec &dst, const Vec &src) {
    dst.insert(dst.end(), src.begin(), src.end());
  };

  size_t total_size = (sizeof_vec(vec0) + ... + sizeof_vec(vecs));

  Vec res;
  res.reserve(total_size);
  (concat_vec(res, vec0), ..., concat_vec(res, vecs));

  return res;
}

/// @brief Splits a string into tokens on delimiter.
/// eg: "A,B,C" --> ["A", "B", "C"]
std::vector<std::string> split_string(const std::string &msg,
                                      const std::string &delim = ",");

/// @brief Remove all whitespaces in a string
std::string remove_whitespaces(std::string x);

void dumpBinary(void *src, size_t length, std::string &filePath);

/// @brief Generate unique time stamp string for current date and time.
std::string generateCurrTimeStamp();

} // namespace Utils

#endif // __UTILS_H_
