from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.build import check_min_cppstd
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
        # cpprestsdk configuration: disable websockets (only HTTP REST used)
        "cpprestsdk/*:with_websockets": False,
        # Never allow fPIC to be True for Boost or cpprestsdk on Windows (prevents b2 -fPIC flag)
        "boost/*:fPIC": False,
        "cpprestsdk/*:fPIC": False,
        # Only build the minimal set of Boost compiled libraries required by cpprestsdk:
        # system, date_time, thread, chrono, atomic, regex, filesystem, random, context, container.
        # Disable all others to avoid MSVC build bugs and slash build times by 80%.
        "boost/*:without_charconv": True,
        "boost/*:without_cobalt": True,
        "boost/*:without_contract": True,
        "boost/*:without_coroutine": True,
        "boost/*:without_fiber": True,
        "boost/*:without_graph": True,
        "boost/*:without_graph_parallel": True,
        "boost/*:without_iostreams": True,
        "boost/*:without_json": True,
        "boost/*:without_locale": True,
        "boost/*:without_log": True,
        "boost/*:without_math": True,
        "boost/*:without_mpi": True,
        "boost/*:without_nowide": True,
        "boost/*:without_process": True,
        "boost/*:without_program_options": True,
        "boost/*:without_python": True,
        "boost/*:without_serialization": True,
        "boost/*:without_stacktrace": True,
        "boost/*:without_test": True,
        "boost/*:without_timer": True,
        "boost/*:without_type_erasure": True,
        "boost/*:without_url": True,
        "boost/*:without_wave": True,
    }
    exports_sources = "CMakeLists.txt", "LICENSE", "include/*", "src/*", "openapi/*", "cmake/*", "tests/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")
        else:
            self.options.fPIC = True

    def configure(self):
        if self.options.shared or self.settings.os == "Windows":
            self.options.rm_safe("fPIC")
        if self.settings.os == "Windows":
            self.options["boost"].fPIC = False
            self.options["cpprestsdk"].fPIC = False

    def validate(self):
        check_min_cppstd(self, "17")

    def requirements(self):
        self.requires("cpprestsdk/2.10.18")
        # Boost 1.84.0 - 1.86.0: fixes MSVC 14.3 b2 build bugs (<1.84) while
        # preserving Boost.Asio APIs required by cpprestsdk dependencies (<1.87).
        self.requires("boost/[>=1.84.0 <1.87.0]", override=True)
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
