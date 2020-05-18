from conans import ConanFile, CMake

class HelloConan(ConanFile):
    name = "dory-external"
    version = "0.0.1"
    license = "MIT"
    description = "External header files"
    options = {
        "shared": [True, False]
    }
    default_options = {"shared": False}
    exports_sources = "src/*"

    def package(self):
        self.copy("*.hpp", dst="include/dory/extern", src="src")

    def package_info(self):
        self.cpp_info.system_libs = ["ibverbs", "memcached"]
