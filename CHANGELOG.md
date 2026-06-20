# Changelog for XAudio HRTF

### Jun 2026
* Adapted to Spherephones rendering intead of HRTF. 

### 2.0 (4 Jul 2017)
* Again some refactoring and prettification of code
* Now different ways of transport spatial data from X3DAudio to XAudio are supported
* Implemented the new way of such transport which simply uses last result from X3DAudio. It also passes volume in matrix, which allows to support situations when calling application implements it's own volume calculation. This enabled Unreal Engine 4 (and some UDK) support
* Added automated build script which outputs final packages for distribution
* Fixed Arma 3 support broken after some of it's updates.
* Fixed spatialized sounds being too loud

### 1.0 (22 Feb 2016)
* Contains both x86 and x64 versions from now on
* Refactored code significantly to support more types of mixing graphs. The surrogate graph is being rebuilt dynamically from now on.
* This improved compatibility a lot, fixed missing sounds in Skyrim
* MIT KEMAR dataset is now included in the package (one of it's pinnae)

### 0.9 (23 Jul 2015) Initial public release
* Works with ArmA 3 and TES V Skyrim, but has some issues

