from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.build import check_min_cppstd, valid_min_cppstd
from conan.tools.files import copy
import os

class XyoSdkCppConan(ConanFile):
    name = "xyo-sdk-cpp"
    version = "2.0.0"
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
        "fPIC": True,
        # Disable heavy / problematic Boost sub-libraries we don't need.
        # contract, context, coroutine, fiber have known MSVC 14.3 source-build
        # failures in Boost < 1.84.0 and are not required by cpprestsdk.
        "boost/*:without_contract": True,
        "boost/*:without_context": True,
        "boost/*:without_coroutine": True,
        "boost/*:without_fiber": True,
        "boost/*:without_wave": True,
        "boost/*:without_stacktrace": True,
        "boost/*:without_python": True,
        "boost/*:without_mpi": True,
    }
    exports_sources = "CMakeLists.txt", "LICENSE", "include/*", "src/*", "openapi/*", "cmake/*", "tests/*"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def validate(self):
        # Fail fast with a clear message — don't let a wrong cppstd silently
        # propagate and blow up 4 minutes deep inside a Boost b2 build.
        check_min_cppstd(self, "17")

    def requirements(self):
        self.requires("cpprestsdk/2.10.18")
        # cpprestsdk/2.10.18 in CCI hard-pins boost/1.83.0 which has known
        # MSVC 14.3 source-build failures (dlmalloc.c, greg_month, exception).
        # All fixed in 1.84.0. override=True wins the graph-wide resolution.
        self.requires("boost/[>=1.84.0 <1.90]", override=True)
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
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.libs = ["xyo_sdk"]
        self.cpp_info.set_property("cmake_file_name", "XYOSDK")
        self.cpp_info.set_property("cmake_target_name", "XYO::SDK")
