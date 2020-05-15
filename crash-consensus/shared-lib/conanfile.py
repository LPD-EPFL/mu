from conans import ConanFile, CMake

class ConensusConan(ConanFile):
    name = "consensus"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "RDMA consensus shared library"
    settings = "os", "compiler", "build_type", "arch"
    # default_options = {
    #     "shared": False,
    #     "fPIC": True,
    #     "log_level": "INFO",

    #     "dory-ctrl:log_level": "INFO",
    #     "dory-connection:log_level": "INFO"
    # }
    generators = "cmake"

    def requirements(self):
        self.requires("dory-crash-consensus/0.0.1")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        self.copy("crash-consensus.hpp", dst="include/dory", src="src")
        self.copy("crash-consensus.h", dst="include/dory", src="src")
        self.copy("*.a", dst="lib", src="lib", keep_path=False)
        self.copy("*.so", dst="lib", src="lib", keep_path=False)

    def imports(self):
        self.copy("crash-consensus.h", src="include/dory", dst="include/dory", keep_path=False)
        self.copy("crash-consensus.hpp", src="include/dory", dst="include/dory", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["consensus"]
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
