from conans import ConanFile, CMake

class DoryCrashConensusConan(ConanFile):
    name = "dory-crash-consensus"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "RDMA crash consensus"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "lto": [True, False],
        "log_level": ["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "lto": True,
        "log_level": "INFO",

        "dory-ctrl:log_level": "INFO",
        "dory-connection:log_level": "INFO"
    }
    generators = "cmake"
    exports_sources = "src/*"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def requirements(self):
        self.requires("dory-connection/0.0.1")
        self.requires("dory-memstore/0.0.1")
        self.requires("dory-ctrl/0.0.1")
        self.requires("dory-shared/0.0.1")
        self.requires("dory-external/0.0.1")

    def build(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator = generator)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)
        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()
        cmake.definitions['SPDLOG_ACTIVE_LEVEL'] = "SPDLOG_LEVEL_{}".format(self.options.log_level)

        cmake.configure(source_folder="src")
        cmake.build()

    def package(self):
        self.copy("crash-consensus.hpp", dst="include/dory", src="src")
        self.copy("crash-consensus.h", dst="include/dory", src="src")
        self.copy("*.a", dst="lib", src="lib", keep_path=False)
        self.copy("*.so", dst="lib", src="lib", keep_path=False)

    def imports(self):
        self.copy("*.a", src="lib", dst="deps", keep_path=False)
        # self.copy("*.so", src="lib", dst="deps", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["dorycrashconsensus"]
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
