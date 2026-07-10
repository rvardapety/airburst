project('airburst', 'c',
        version : '1.0.0',
        default_options : ['warning_level=3', 'default_library=static'])

include_directories = include_directories('include')

sources = [
    'src/airburst.c'
]

dependencies = [
    dependency('a111')
]

# dependency
airburst_dep = declare_dependency(
    include_directories: include_directories,
    sources: sources,
    dependencies: dependencies,
)
meson.override_dependency(meson.project_name(), airburst_dep)

# library
if get_option('library').enabled()
    airburst_lib = library(
        meson.project_name(),
        include_directories: include_directories,
        sources: sources,
        dependencies: dependencies,
    )
endif

