from conans import ConanFile, CMake


class DoryCryptoConan(ConanFile):
    name = "dory-crypto"
    version = "0.0.1"
    license = "MIT"
    # url = "TODO"
    description = "Crypto module"
    topics = "crypto"
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
    default_options = {"shared": False, "log_level": "INFO", "lto": True}
    generators = "cmake"
    exports_sources = (
        "src/*",
        "lib/*.a",
        "lib/*.so",
        "!lib/**/target/*.a",
        "!lib/**/target/*.so",
    )
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def _configure_cmake(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator = generator)

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
        self.requires("dory-shared/0.0.1")
        self.requires("dory-memstore/0.0.1")
        self.requires("libsodium/1.0.18")

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("*.hpp", dst="include/dory/crypto/sign", src="src/sign")
        self.copy("*.a", dst="lib", keep_path=False, src="lib")
        self.copy("*.so", dst="lib", keep_path=False, src="lib")

    def package_info(self):
        self.cpp_info.libs = ["dorycrypto"]
        self.cpp_info.cxxflags = self.python_requires[
            "dory-compiler-options"
        ].module.get_cxx_options_for(self.settings.compiler, self.settings.build_type)
