g++ -std=c++26 -O3 -march=native -I./include -I./src `
    "D:\TataG\DGTL\tests\functional\pipeline\production_arch_validator.cpp" `
    (Get-ChildItem .\src\*.cpp -Exclude "main.cpp").FullName `
    (Get-ChildItem .\src\ir\*.cpp).FullName `
    (Get-ChildItem .\src\graph\*.cpp).FullName `
    (Get-ChildItem .\src\ir\evolution\*.cpp).FullName `
    (Get-ChildItem .\src\jit\speculation\*.cpp).FullName `
    -o .\build\production_validator.exe -mconsole