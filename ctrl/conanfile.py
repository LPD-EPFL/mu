from conans import ConanFile, CMake

class HelloConan(ConanFile):
    name = "dory-ctrl"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "RDMA control over native ibverbs"
    topics = ("rdma", "ibverbs")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    generators = "cmake"
    exports_sources = "src/*"

    def requirements(self):
        self.requires("dory-shared/0.0.1")

    def build(self):
        cmake = CMake(self)
        cmake.configure(source_folder="src")
        cmake.build()

    def package(self):
        self.copy("*.hpp", dst="include/dory/ctrl", src="src")
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["ctrl"]
        self.cpp_info.system_libs = ["ibverbs"]
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
