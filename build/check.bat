@echo off
cd /d D:\Documents\Programming\C_Projects\QEngine
C:\msys64\ucrt64\bin\g++.exe -std=gnu++20 -Isrc -Iextern/entt/single_include -Iextern/glfw/include -Iextern/glad/include -Iextern/glm -Iextern/stb -fsyntax-only src/engine/ecs/scene_setup.cpp > build\check_out.txt 2>&1
echo EXIT: %ERRORLEVEL% >> build\check_out.txt
