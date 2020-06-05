from conans import ConanFile

options = {
    "BUILD_TYPE": {
        "DEBUG": [
            # "-O3" agressively inlines functions causing -Winline to trigger
            # warnings, so we only use it in debug mode
            # Warn if a function that is declared as inline cannot be inlined
            "-Winline",
            # Generate source-level debug information
            "-g",
        ],
        "RELEASE": [
            # Optimizations as most as possible
            "-O3",
            # Define NDEBUG
            "-DNDEBUG",
        ],
        "RELWITHDEBINFO": [
            # Perform nearly all supported optimizations that do not involve a
            # space-speed tradeoff
            "-O2",
            # Generate source-level debug information
            "-g",
            # Define NDEBUG
            "-DNDEBUG",
        ],
        "MINSIZEREL": [
            # Optimize for size
            "-Os",
            # Define NDEBUG
            "-DNDEBUG",
        ],
        # Explicitly create this type to comply with conan
        "NONE": [],
    },
    "TARGET": {
        "GCC": {
            "C": [
                # Warn about suspicious uses of logical operators in expressions
                "-Wlogical-op",
            ],
            "CXX": [
                # Warn about suspicious uses of logical operators in expressions
                "-Wlogical-op",
                # Warn about the use of an uncasted NULL as sentinel
                "-Wstrict-null-sentinel",
                # Warn when a noexcept-expression evaluates to false because of a call
                # to a function that does not have a non-throwing exception
                # specification (i.e. throw() or noexcept) but is known by the compiler
                # to never throw an exception.
                "-Wnoexcept",
            ],
        },
        "CLANG": {"C": [], "CXX": []},
    },
    "LANG": {
        "CXX": [
            # Generates a warning when all constructors and destructors in a class
            # are private and therefore inaccessible
            "-Wctor-dtor-privacy",
            # Warn when overload resolution chooses a promotion from unsigned or
            # enumerated type to a signed type, over a conversion to an unsigned
            # type of the same size
            "-Wsign-promo",
            # Warn when a function declaration hides virtual functions from a base
            # class
            "-Woverloaded-virtual",
            # Warn if an old-style (C-style) cast to a non-void type is used within
            # a C++ program. The new-style casts (dynamic_cast, static_cast,
            # reinterpret_cast, and const_cast) are less vulnerable to unintended
            # effects and much easier to search for.
            "-Wold-style-cast",
        ],
        "C": [],
    },
    "GENERAL": [
        # Make all warnings into errors
        "-Werror",
        # Turns on the above mentioned flags
        "-Wall",
        # Warn about implicit conversions
        "-Wconversion",
        # Warn if floating-point values are used in equality comparisons
        "-Wfloat-equal",
        # Issue all the warnings demanded by strict ISO C and ISO C++
        "-Wpedantic",
        # Warn about anything that depends on the “size of” a function type or
        # of void.
        "-Wpointer-arith",
        # Warn whenever a switch statement has an index of enumerated type and
        # lacks a case for one or more of the named codes of that enumeration.
        # This option gives a warning about an omitted enumeration code even if
        # there is a default label.
        # "-Wswitch-enum",
        # Warn whenever a switch statement does not have a default case.
        "-Wswitch-default",
        # Warn if a structure is given the packed attribute, but the packed
        # attribute has no effect on the layout or size of the structure
        "-Wpacked",
        # This enables some extra warning flags that are not enabled by -Wall
        "-Wextra",
        # Warn if a precompiled header is found in the search path but cannot
        # be used.
        "-Winvalid-pch",
        # Warn if a structure’s initializer has some fields missing
        "-Wmissing-field-initializers",
        # Warn if the compiler detects that code will never be executed
        "-Wunreachable-code",
        # Warn whenever a pointer is cast such that the required alignment of
        # the target is increased
        "-Wcast-align",
        # Warn whenever a pointer is cast so as to remove a type qualifier from
        # the target type.
        "-Wcast-qual",
        # Warn if a requested optimization pass is disabled
        "-Wdisabled-optimization",
        # Enable -Wformat plus additional format checks (- Check calls to printf
        # and scanf, etc., to make sure that the arguments supplied have types
        # appropriate to the format string specified, and that the conversions
        # specified in the format string make sense)
        "-Wformat=2",
        # If -Wformat is specified, also warn if the format string is not a
        # string literal and so cannot be checked, unless the format function
        # takes its format arguments as a va_list.
        "-Wformat-nonliteral",
        # Warn if an automatic variable is used without first being initialized.
        "-Wuninitialized",
        # If -Wformat is specified, also warn about uses of format functions
        # that represent possible security problems
        "-Wformat-security",
        # If -Wformat is specified, also warn about strftime formats that may
        # yield only a two-digit year
        "-Wformat-y2k",
        # Warn about uninitialized variables that are initialized with
        # themselves
        "-Winit-self",
        # Warn if a global function is defined without a previous declaration
        "-Wmissing-declarations",
        # Warn if a user-supplied include directory does not exist
        "-Wmissing-include-dirs",
        # Warn if anything is declared more than once in the same scope, even in
        # cases where multiple declaration is valid and changes nothing.
        "-Wredundant-decls",
        # This option is only active when signed overflow is undefined. It warns
        # about cases where the compiler optimizes based on the assumption that
        # signed overflow does not occur. (=5 : Also warn about cases where the
        # compiler reduces the magnitude of a constant involved in a comparison)
        "-Wstrict-overflow=5",
        # Warn if an undefined identifier is evaluated in an #if directive
        "-Wundef",
        # Enables unused warnings as:
        #   -Wunused-function,
        #   -Wunused-label,
        #   -Wunused-value,
        #   -Wunused-variable,
        #   -Wsign-conversion
        "-Wno-unused",
    ],
}


def get_cxx_options_for(target, build_type):
    return (
        options["GENERAL"]
        + options["LANG"]["CXX"]
        + options["TARGET"][str(target).upper()]["CXX"]
        + options["BUILD_TYPE"][str(build_type).upper()]
    )


def set_options(cmake):
    compiler = cmake.definitions["CONAN_COMPILER"].upper()
    build_type = cmake.definitions["CMAKE_BUILD_TYPE"].upper()

    cmake.definitions["CMAKE_C_FLAGS"] = " ".join(
        options["GENERAL"] + options["LANG"]["C"] + options["TARGET"][compiler]["C"]
    )
    cmake.definitions["CMAKE_CXX_FLAGS"] = " ".join(
        options["GENERAL"] + options["LANG"]["CXX"] + options["TARGET"][compiler]["CXX"]
    )
    cmake.definitions["CMAKE_CXX_FLAGS_{}".format(build_type)] = " ".join(
        options["BUILD_TYPE"][build_type]
    )
    cmake.definitions["CMAKE_C_FLAGS_{}".format(build_type)] = " ".join(
        options["BUILD_TYPE"][build_type]
    )


class CompilerOptions(ConanFile):
    name = "dory-compiler-options"
    version = "0.0.1"
