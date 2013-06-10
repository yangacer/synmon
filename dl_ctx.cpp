#include "dl_ctx.hpp"
#include <cassert>
#include <cstring>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
namespace ipc = boost::interprocess;

dl_ctx::dl_ctx()
  : off_(0)
{}

dl_ctx::~dl_ctx()
{}

dl_ctx::dl_ctx(std::string const &filename, size_t size)
  : off_(0)
{
  open(filename, size);
}

dl_ctx::operator bool() const
{
  return !orig_file_.empty();
}

void dl_ctx::open(std::string const &filename, size_t size)
{
  if(*this) close();
  orig_file_ = filename;
  size_ = size;
  // gen temp file
  fs::path orig(orig_file_);
  fs::path tmp = fs::unique_path( orig.parent_path() / "%%%%-%%%%-%%%%.tmp");
  tmp_file_ = tmp.string();
  { std::ofstream f(tmp_file_);  }
  fs::resize_file(tmp, size); // may throw exception
  // map to memory
  mf_ = ipc::file_mapping(tmp_file_.c_str(), ipc::read_write);
  mr_ = ipc::mapped_region(mf_, ipc::read_write);
}

void dl_ctx::close()
{
  orig_file_.clear();
  tmp_file_.clear();
  off_ = 0;
}

void dl_ctx::append(char const *data, size_t size)
{
  assert(off_ + size <= size_ && "exceepd expected size");
  char* addr = (char*)mr_.get_address() + off_;
  std::memcpy(addr, data, size);
  off_ += size;
}

void dl_ctx::commit()
{
  mr_.flush();
  mr_ = ipc::mapped_region();
  mf_ = ipc::file_mapping();

  fs::path orig(orig_file_);
  fs::path tmp(tmp_file_);
  fs::rename(tmp, orig);
  fs::resize_file(orig, size_);
}
