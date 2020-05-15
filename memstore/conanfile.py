from conans import ConanFile, CMake

class HelloConan(ConanFile):
    name = "dory-memstore"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "Sting based in memory key-value store"
    topics = ("key-value", "store", "memcached")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True
    }
    generators = "cmake"
    exports_sources = "src/*"

    def requirements(self):
        self.requires("dory-shared/0.0.1")
        self.requires("dory-external/0.0.1")

    def build(self):
        cmake = CMake(self)
        cmake.configure(source_folder="src")
        cmake.build()

    def package(self):
        self.copy("*.hpp", dst="include/dory", src="src")
        self.copy("*.a", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["memstore"]
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
