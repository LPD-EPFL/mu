from conans import ConanFile, CMake


class DoryDemoConan(ConanFile):
    name = "dory-demo"
    version = "0.0.1"
    license = "MIT"
    # url = "TODO"
    description = "RDMA demo"
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
        "shared": False,
        "lto": True,
    }
    generators = "cmake"
    exports_sources = "src/*"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def configure(self):
        pass

    def requirements(self):
        self.requires("dory-crash-consensus/0.0.1")

    def build(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator = generator)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)
        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()

        cmake.configure()
        cmake.build()
