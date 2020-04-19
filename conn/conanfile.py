from conans import ConanFile, CMake

class DoryConnectionConan(ConanFile):
    name = "dory-connection"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "RDMA connection abstractions"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "log_level": ["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"]}
    default_options = {"shared": False, "log_level": "INFO",
        "dory-ctrl:log_level": "OFF"}
    generators = "cmake"
    exports_sources = "src/*"


    def _configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions['SPDLOG_ACTIVE_LEVEL'] = "SPDLOG_LEVEL_{}".format(self.options.log_level)
        cmake.configure(source_folder="src")
        return cmake

    def requirements(self):

        self.requires("dory-shared/0.0.1")
        self.requires("dory-ctrl/0.0.1")
        self.requires("dory-memstore/0.0.1")

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("*.hpp", dst="include/dory/conn", src="src")
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["conn"]
        self.cpp_info.cxxflags = ["-std=c++17", "-g", "-O3", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-unused-result"]
