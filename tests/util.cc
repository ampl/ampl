/*
 Test utilities

 Copyright (C) 2013 AMPL Optimization Inc

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization Inc disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.

 Author: Victor Zverovich
 */

#include "tests/util.h"

#include "solvers/util/error.h"

extern "C" {
#include "solvers/asl.h"
}
#undef filename

#include <fstream>

#ifdef WIN32
# include <direct.h>
# define chdir _chdir
#else
# include <unistd.h>
#endif

std::string ReadFile(fmt::StringRef name) {
  std::string data;
  std::ifstream ifs(name.c_str());
  enum { BUFFER_SIZE = 4096 };
  char buffer[BUFFER_SIZE];
  do {
    ifs.read(buffer, BUFFER_SIZE);
    data.append(buffer, static_cast<std::string::size_type>(ifs.gcount()));
  } while (ifs);
  return data;
}

void WriteFile(fmt::StringRef name, fmt::StringRef data) {
  std::ofstream ofs(name.c_str());
  ofs.write(data.c_str(), data.size());
}

StderrRedirect::StderrRedirect(const char *filename) : saved_stderr(Stderr) {
  Stderr = std::fopen(filename, "w");
}

StderrRedirect::~StderrRedirect() {
  std::fclose(Stderr);
  Stderr = saved_stderr;
}

void ChangeDirectory(fmt::StringRef path) {
  if (chdir(path.c_str()) != 0)
    ampl::ThrowError("chdir failed, error code = {}") << errno;
}

void ExecuteShellCommand(fmt::StringRef command) {
#ifdef WIN32
  std::string command_str(command);
  std::replace(command_str.begin(), command_str.end(), '/', '\\');
#else
  fmt::StringRef command_str(command);
#endif
  if (std::system(command_str.c_str()) != 0)
    ampl::ThrowError("std::system failed, error code = {}") << errno;
}

std::vector<std::string> Split(const std::string &s, char sep) {
  std::vector<std::string> items;
  std::string::size_type start = 0, end = 0;
  while ((end = s.find(sep, start)) != std::string::npos) {
    items.push_back(s.substr(start, end - start));
    start = end + 1;
  }
  items.push_back(s.substr(start));
  return items;
}
