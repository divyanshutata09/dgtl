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

#include <iostream>
#include <windows.h>
#include <cassert>

int main() {
    std::cout << "[TEST] Validating Windows Zero-Copy Memory Mapping..." << std::endl;

    const char* filepath = "dummy_weights.bin";
    
    // 1. Create a dummy file
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(hFile != INVALID_HANDLE_VALUE && "Failed to create dummy file");
    
    // Set file size to 1 Gigabyte
    size_t length = 1024 * 1024 * 1024; 
    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = length;
    SetFilePointerEx(hFile, liDistanceToMove, NULL, FILE_BEGIN);
    SetEndOfFile(hFile);

    // 2. Create the File Mapping Object (The core of Windows zero-copy)
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
    assert(hMap != NULL && "CreateFileMapping failed! Engine cannot handle large models.");

    // 3. Map it to Virtual Memory
    void* ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, length);
    assert(ptr != NULL && "MapViewOfFile failed!");

    // 4. Test Memory Access (Bypassing the heap)
    char* tensor_data = static_cast<char*>(ptr);
    tensor_data[length - 1] = 42; // Write to the very end of the 1GB block
    
    assert(tensor_data[length - 1] == 42 && "Memory mapping corrupted data.");

    // Cleanup OS handles
    UnmapViewOfFile(ptr);
    CloseHandle(hMap);
    CloseHandle(hFile);
    DeleteFileA(filepath);
    
    std::cout << "[PASS] Memory Mapping successful on Windows. Ready for .bin weights." << std::endl;
    return 0;
}