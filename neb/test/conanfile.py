from conans import ConanFile, CMake


class DoryNebTestConan(ConanFile):
    name = "dory-neb-test"
    version = "0.0.1"
    description = "tests for dory's neb abstraction"
    settings = {
        "os": None,
        "compiler": {
            "gcc": {"libcxx": "libstdc++11", "cppstd": ["17", "20"], "version": None},
            "clang": {"libcxx": "libstdc++11", "cppstd": ["17", "20"], "version": None},
        },
        "build_type": None,
        "arch": None,
    }
    options = {"shared": [True, False], "lto": [True, False]}
    default_options = {
        "shared": False,
        "lto": True,
    }
    generators = "cmake"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def requirements(self):
        self.requires("gtest/1.10.0")
        self.requires("dory-shared/0.0.1")

    def build(self):
        cmake = CMake(self)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)
        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()

        cmake.configure(source_folder=".")
        cmake.build()

    def test(self):
        self.run("./bin/main")

