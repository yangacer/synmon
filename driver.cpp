#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <boost/bind.hpp>

#include "synmon.hpp"
#include "dl_ctx.hpp"

using namespace std;

typedef int (*test_case)(int, char**);
typedef std::map<std::string, test_case> test_case_map;

void usage()
{
  cout << 
    "Usage: driver <cmd> <arg>\n" <<
    "cmd:\n\n"
    " dl_ctx <filename> <size>\n"
    ;
  exit(1);
}

int dl_ctx_test(int argc, char **argv)
{
  if(argc < 2) exit(1);
  size_t size = strtoul(argv[1], NULL, 10);
  dl_ctx dc(argv[0], size);
  char data;
  for(size_t i = 0; i < size; ++i) {
    data = 'a' + (char)(i % 26);
    dc.append(&data, 1);
  }
  dc.commit();
  // verify
  string buf;
  buf.resize(size);
  ifstream fin(argv[0], ios::binary | ios::in);
  if(!fin.is_open())
    return 1;
  fin.read(&buf[0], size);

  for(size_t i = 0; i < size; ++i) {
    data = 'a' + (char)(i % 26);
    if(buf[i] != data) return 1;
  }
  return 0;
}

int main(int argc, char **argv)
{
  if(argc < 2) usage();
  argc--; argv++;

  test_case_map tcm;
  tcm["dl_ctx"] = &dl_ctx_test;

  int rt = 0;
  if(tcm.count(argv[0])) {
    cout << "execute command: ";
    for(int i= 0; i < argc; ++i)
      cout << argv[i] << " ";
    cout << "\n";
    rt = (*tcm[argv[0]])(--argc, ++argv);
  } else {
    usage();
  }

  return rt;
}
