:This install script builds zimg v3.0.6 official MSVC project with Microsoft Visual Studio 2022.
:Before running this install script, make sure MSBuild is in your PATH.

git clone --revision f819b14e8f39d1282400b0d9543e8ef73c1b2bbd https://github.com/sekrit-twc/zimg.git
cd zimg
git clone --revision f06d7cb4d589ea4657f01b13613efb7437c8ecda https://github.com/sekrit-twc/graphengine
MSBuild -p:Configuration=Debug ./_msvc/zimg/zimg.vcxproj
MSBuild -p:Configuration=Release ./_msvc/zimg/zimg.vcxproj
cd ..
