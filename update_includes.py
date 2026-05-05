import os
import re
from collections import defaultdict

# 1. Build a map of filename to its new paths
include_dir = 'include'
src_dirs = ['include', 'src', 'tests', 'apps', 'tools']
file_to_paths = defaultdict(list)

for root, _, files in os.walk(include_dir):
    for f in files:
        if f.endswith('.hpp') or f.endswith('.h'):
            rel_path = os.path.relpath(os.path.join(root, f), include_dir).replace('\\\\', '/')
            file_to_paths[f].append(rel_path)

# Known exact replacements for previously moved items from api -> core
exact_replacements = {
    'dgtl/api/ABI.hpp': 'dgtl/core/ABI.hpp',
    'dgtl/api/Backend_Driver_API.hpp': 'dgtl/core/Backend_Driver_API.hpp',
    'dgtl/api/ControlFlow.hpp': 'dgtl/core/ControlFlow.hpp',
    'dgtl/api/FrontendHelpers.hpp': 'dgtl/core/FrontendHelpers.hpp',
    'dgtl/api/Frontend_Payload.hpp': 'dgtl/core/Frontend_Payload.hpp',
    'dgtl/api/Function.hpp': 'dgtl/core/Function.hpp',
    'dgtl/api/Symbol_Table.hpp': 'dgtl/core/Symbol_Table.hpp',
    'dgtl/all.hpp': 'dgtl/dgtl_all.hpp',
}

def update_includes_and_namespaces(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except:
        return # Skip non utf-8 or non-existent
        
    original = content
    
    # Replace exact known paths
    for old_inc, new_inc in exact_replacements.items():
        content = content.replace(f'#include "{old_inc}"', f'#include "{new_inc}"')
        content = content.replace(f'#include <{old_inc}>', f'#include <{new_inc}>')

    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)

for d in src_dirs:
    if os.path.exists(d):
        for root, _, files in os.walk(d):
            for f in files:
                if f.endswith('.cpp') or f.endswith('.hpp') or f.endswith('.h'):
                    update_includes_and_namespaces(os.path.join(root, f))
print('Include paths mapped.')
