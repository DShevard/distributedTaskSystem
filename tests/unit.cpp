#include "common.hpp"
#include <cassert>
using namespace dts;

int main(){
    assert(split("TASK 1 2 backup 100") == std::vector<std::string>({"TASK","1","2","backup","100"}));
    assert(valid_name("backup_01"));
    assert(!valid_name("foo bar"));
    assert(!valid_name("bad\nname"));
    assert(!valid_name(""));
}
