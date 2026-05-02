from os import path

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain

class CompressorRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    # The requirements() method is used to specify the dependencies of a package.
    # https://docs.conan.io/2.0/reference/conanfile/methods/requirements.html
    def requirements(self):
        self.requires("spdlog/1.13.0")
        self.requires("protobuf/5.27.0")
        self.requires("grpc/1.67.1")

    # The build_requirements() method is used to define tool_requires and test_requires:
    # https://docs.conan.io/2/reference/conanfile/methods/build_requirements.html
    def build_requirements(self):
        self.tool_requires("ninja/1.11.1")

    # The cmake_layout() sets the folders and cpp attributes to follow the structure of a typical CMake project.
    # https://docs.conan.io/2.0/reference/tools/cmake/cmake_layout.html
    def layout(self):
        cmake_layout(self)
        self.folders.build  = path.join("build", str(self.settings.build_type))
        self.folders.generators = path.join(self.folders.build, "generators")

    # The purpose of generate() is to prepare the build, generating the necessary files.
    # https://docs.conan.io/2.0/reference/conanfile/methods/generate.html
    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
        tc.generate()

    # The build() method is used to define the build from source of the package.
    # https://docs.conan.io/2.0/reference/conanfile/methods/build.html
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    # The package() method is in charge of copying files from the source_folder and the temporary build_folder to the package_folder
    ## https://docs.conan.io/2/reference/conanfile/methods/package.html#
    def package(self):
        cmake = CMake(self)
        cmake.install()