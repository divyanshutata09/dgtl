/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

g++ -std=c++26 -O3 -march=native -I./include -I./src `
    "D:\TataG\DGTL\tests\functional\pipeline\production_arch_validator.cpp" `
    (Get-ChildItem .\src\*.cpp -Exclude "main.cpp").FullName `
    (Get-ChildItem .\src\ir\*.cpp).FullName `
    (Get-ChildItem .\src\graph\*.cpp).FullName `
    (Get-ChildItem .\src\ir\evolution\*.cpp).FullName `
    (Get-ChildItem .\src\\speculation\*.cpp).FullName `
    -o .\build\production_validator.exe -mconsole