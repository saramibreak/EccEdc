# currently windows only

project(
  'EccEdc',
  'c',
  'cpp',
  default_options : ['buildtype=release', 'cpp_std=c++11']
)

conf_data = configuration_data()

src_files = files(
  # external
  'EccEdc/_external/ecm.cpp',
  'EccEdc/_external/ecm.h',
  'EccEdc/FileUtils.cpp',
  'EccEdc/FileUtils.hpp',
  'EccEdc/StringUtils.cpp',
  'EccEdc/StringUtils.hpp',
  # EccEdc
  'EccEdc/Enum.h',
  'EccEdc/EccEdc.cpp',

)

all_sources = [src_files]

inc = [include_directories('EccEdc/'), include_directories('EccEdc/_external')]

executable(
  meson.project_name(),
  sources: [
  all_sources
  ],
  include_directories: inc,
)
