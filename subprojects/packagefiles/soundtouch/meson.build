project('soundtouch', 'cpp',
  version: '2.3.3',
  meson_version: '>=0.60.0',
  default_options: ['cpp_std=c++17'])

cxx = meson.get_compiler('cpp')

soundtouch_inc = include_directories('include')

common_args = ['-DSOUNDTOUCH_FLOAT_SAMPLES']
simd_args = []

cpu = host_machine.cpu_family()
if cpu == 'aarch64'
  common_args += ['-DSOUNDTOUCH_USE_NEON']
elif cpu == 'arm'
  if cxx.has_argument('-mfpu=neon')
    common_args += ['-DSOUNDTOUCH_USE_NEON']
    simd_args += ['-mfpu=neon']
  endif
endif

soundtouch_sources = [
  'source/SoundTouch/AAFilter.cpp',
  'source/SoundTouch/BPMDetect.cpp',
  'source/SoundTouch/cpu_detect_x86.cpp',
  'source/SoundTouch/FIFOSampleBuffer.cpp',
  'source/SoundTouch/FIRFilter.cpp',
  'source/SoundTouch/InterpolateCubic.cpp',
  'source/SoundTouch/InterpolateLinear.cpp',
  'source/SoundTouch/InterpolateShannon.cpp',
  'source/SoundTouch/mmx_optimized.cpp',
  'source/SoundTouch/PeakFinder.cpp',
  'source/SoundTouch/RateTransposer.cpp',
  'source/SoundTouch/SoundTouch.cpp',
  'source/SoundTouch/sse_optimized.cpp',
  'source/SoundTouch/TDStretch.cpp',
]

soundtouch = library(
  'soundtouch',
  soundtouch_sources,
  include_directories: soundtouch_inc,
  cpp_args: common_args + simd_args,
  install: false)

soundtouch_dep = declare_dependency(
  link_with: soundtouch,
  include_directories: soundtouch_inc,
  compile_args: common_args + simd_args)
