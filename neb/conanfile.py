from conans import ConanFile, CMake


class DoryNebConan(ConanFile):
    name = "dory-neb"
    version = "0.0.1"
    license = "MIT"
    # url = "TODO"
    description = "RDMA non-equivocating broadcast"
    settings = {
        "os": None,
        "compiler": {
            "gcc": {"libcxx": "libstdc++11", "cppstd": ["17", "20"], "version": None},
            "clang": {"libcxx": "libstdc++11", "cppstd": ["17", "20"], "version": None},
        },
        "build_type": None,
        "arch": None,
    }
    options = {
        "shared": [True, False],
        "log_level": ["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"],
        "lto": [True, False],
    }
    default_options = {
        "shared": False,
        "log_level": "INFO",
        "dory-ctrl:log_level": "INFO",
        "dory-connection:log_level": "INFO",
        "dory-crypto:log_level": "INFO",
        "lto": True,
    }
    generators = "cmake"
    exports_sources = "src/*"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def _configure_cmake(self):
        cmake = CMake(self)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)

        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()
        cmake.definitions["SPDLOG_ACTIVE_LEVEL"] = "SPDLOG_LEVEL_{}".format(
            self.options.log_level
        )
        cmake.configure(source_folder="src")
        return cmake

    def configure(self):
        pass

    def requirements(self):
        self.requires("dory-connection/0.0.1")
        self.requires("dory-memstore/0.0.1")
        self.requires("dory-ctrl/0.0.1")
        self.requires("dory-shared/0.0.1")
        self.requires("dory-external/0.0.1")
        self.requires("dory-crypto/0.0.1")

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("sync.hpp", dst="include/dory/neb", src="src")
        self.copy("consts.hpp", dst="include/dory/neb", src="src")
        self.copy("broadcastable.hpp", dst="include/dory/neb", src="src")
        self.copy("*.a", dst="lib", src="lib", keep_path=False)
        self.copy("*.so", dst="lib", src="lib", keep_path=False)

    def imports(self):
        self.copy("*.a", src="lib", dst="deps", keep_path=False)
        self.copy("*.so", src="lib", dst="deps", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["doryneb"]
        self.cpp_info.cxxflags = self.python_requires[
            "dory-compiler-options"
        ].module.get_cxx_options_for(self.settings.compiler, self.settings.build_type)
