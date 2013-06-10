#ifndef SYNMON_DL_CTX_HPP_
#define SYNMON_DL_CTX_HPP_

#include <string>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/noncopyable.hpp>

struct dl_ctx : boost::noncopyable
{
  dl_ctx();
  ~dl_ctx();
  dl_ctx(std::string const &filename, size_t size);
  operator bool() const;
  void open(std::string const &filename, size_t size);
  void close();
  void append(char const *data, size_t size);
  void commit();
private:
  std::string orig_file_;
  std::string tmp_file_;
  size_t size_;
  boost::interprocess::file_mapping mf_;
  boost::interprocess::mapped_region mr_;
  boost::interprocess::offset_t off_;
};

#endif
