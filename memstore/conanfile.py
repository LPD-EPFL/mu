from conans import ConanFile, CMake


class DoryMemstoreConan(ConanFile):
    name = "dory-memstore"
    version = "0.0.1"
    license = "MIT"
    # url = "TODO"
    description = "String based in memory key-value store"
    topics = ("key-value", "store", "memcached")
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
    default_options = {"shared": False, "lto": True}
    generators = "cmake"
    exports_sources = "src/*"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def configure(self):
        pass

    def requirements(self):
        self.requires("dory-shared/0.0.1")
        self.requires("dory-external/0.0.1")

    def build(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator = generator)

        self.python_requires["dory-compiler-options"].module.set_options(cmake)
        cmake.definitions["DORY_LTO"] = str(self.options.lto).upper()

        cmake.configure(source_folder="src")
        cmake.build()

    def package(self):
        self.copy("*.hpp", dst="include/dory", src="src")
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["dorymemstore"]
        self.cpp_info.cxxflags = self.python_requires[
            "dory-compiler-options"
        ].module.get_cxx_options_for(self.settings.compiler, self.settings.build_type)
