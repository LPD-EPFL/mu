from conans import ConanFile, CMake

class CrashConensusConan(ConanFile):
    name = "dory-crash-consensus-shared"
    version = "0.0.1"
    license = "MIT"
    #url = "TODO"
    description = "RDMA consensus shared library"
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
        "lto": [True, False],
    }

    default_options = {
        "shared": True,
        "lto": True,
    }

    generators = "cmake"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"
    exports_sources = "src/*"

    def requirements(self):
        self.requires("dory-crash-consensus/0.0.1")

    def build(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator=generator)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)
        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()

        # build_type = cmake.definitions["CMAKE_BUILD_TYPE"].upper()
        # print(">>>>", build_type)

        cmake.configure(source_folder="src")
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
        self.cpp_info.libs = ["crashconsensus"]
        self.cpp_info.cxxflags = self.python_requires[
            "dory-compiler-options"
        ].module.get_cxx_options_for(self.settings.compiler, self.settings.build_type)
