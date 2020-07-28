from conans import ConanFile, CMake


class DoryExternals(ConanFile):
    name = "dory-external"
    version = "0.0.1"
    license = "MIT"
    description = "External header files"
    generators = "cmake"
    exports_sources = "src/*"
    python_requires = "dory-compiler-options/0.0.1@dory/stable"

    def build(self):
        generator = self.python_requires["dory-compiler-options"].module.generator()
        cmake = CMake(self, generator = generator)
        cmake.configure(source_folder="src")

    def package(self):
        self.copy("*.hpp", dst="include/dory/extern", src="src")

    def package_info(self):
        self.cpp_info.system_libs = ["memcached", "ibverbs"]
