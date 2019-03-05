#define CATCH_CONFIG_RUNNER

#include "catch.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
	auto returnCode = Catch::Session().run(argc, argv);
	std::cin.get();
	return returnCode;

}