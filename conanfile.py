from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.build import check_min_cppstd
from conan.tools.files import copy
import os


class XyoSdkCppConan(ConanFile):
    name = "xyo-sdk"
    version = "2.2.0"
    license = "Apache-2.0"
    author = "Syniol Limited"
    url = "https://github.com/xyo-financial/sdk-cpp"
    description = "XYO SDK to connect and consume AI Banking Transaction Enrichment API"
    topics = ("xyo", "banking", "enrichment", "sdk")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
    }
    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "include/*",
        "src/*",
        "cmake/*",
        "tests/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")
        else:
            self.options.fPIC = True

    def configure(self):
        if self.options.shared or self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def validate(self):
        check_min_cppstd(self, "17")

    def requirements(self):
        self.requires("cpr/1.10.5")
        self.requires("nlohmann_json/3.11.3")
        self.requires("zlib/[>=1.2.11 <2]")
        self.requires("openssl/[>=1.1.1 <4]")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.libs = ["xyo_sdk"]
        self.cpp_info.set_property("cmake_file_name", "XYOSDK")
        self.cpp_info.set_property("cmake_target_name", "XYO::SDK")
