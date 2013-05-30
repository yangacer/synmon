#include <windows.h>
#include <string>
#include <iostream>

int main(int argc, char **argv)
{
  using namespace std;
  string in(argv[1]);
  for(auto i = in.begin(); i != in.end(); ++i) {
    cout << hex << (unsigned int)*i << "\n";
  }
  return 0;
}
