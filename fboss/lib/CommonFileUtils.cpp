// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/lib/CommonFileUtils.h"
#include <boost/filesystem/operations.hpp>
#include "fboss/agent/SysError.h"

#include <fcntl.h>

namespace facebook::fboss {

bool removeFile(const std::string& filename) {
  int rv = unlink(filename.c_str());
  if (rv == 0) {
    // The file existed and we successfully removed it.
    return true;
  }
  if (errno == ENOENT) {
    // The file wasn't present.
    return false;
  }
  // Some other unexpected error.
  throw SysError(errno, "error while trying to remove file: ", filename);
}

int createFile(const std::string& filename) {
  mode_t mode = S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP;
  auto fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
  if (fd < 0) {
    // Some other unexpected error.
    throw SysError(errno, "error while trying to create file ", filename);
  }
  return fd;
}

void createDir(folly::StringPiece path) {
  try {
    boost::filesystem::create_directories(path.str());
  } catch (...) {
    throw SysError(errno, "failed to create directory \"", path, "\"");
  }
}

bool checkFileExists(const std::string& filename) {
  // No need to catch exception as this function is BOOST_NOEXCEPT
  return boost::filesystem::exists(filename);
}

boost::filesystem::recursive_directory_iterator recursive_directory_iterator(
    const std::string& path) {
  return boost::filesystem::recursive_directory_iterator(path);
}

} // namespace facebook::fboss
