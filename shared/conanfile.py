from conans import ConanFile, CMake

class HelloConan(ConanFile):
    name = "dory-shared"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "Shared sources"
    exports_sources = "src/*"

    def package(self):
        self.copy("*.hpp", dst="include/dory/shared", src="src")
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
